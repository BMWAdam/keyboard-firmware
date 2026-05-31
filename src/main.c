#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
 
#include <pico/stdlib.h>
#include <pico/stdio.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
 
#include <FreeRTOS.h>
#include <task.h>
 
#include "tusb.h"
#include "pico/cyw43_arch.h"
#include "leds.h"
#include "class/hid/hid.h"
 
#include "cJSON.h"
 
#define CDC_TASK_PRIORITY          2
#define USB_TASK_PRIORITY          3
#define KEY_SCANNING_TASK_PRIORITY 4
 
#define MAX_LAYERS 2
#define MAX_KEYS   61
 
#define FLASH_TARGET_OFFSET (1024 * 1024)
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
 
#define NKRO_KEYCODE_BYTES  16
#define NKRO_REPORT_SIZE    (1 + 1 + NKRO_KEYCODE_BYTES)
 
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[NKRO_KEYCODE_BYTES];
} nkro_report_t;
 
// Set one keycode bit inside an NKRO report
static inline void nkro_set_key(nkro_report_t *r, uint8_t kc) {
    if (kc < 128) r->keys[kc >> 3] |= (1u << (kc & 7));
}
 
typedef enum {
    ACT_NONE,
    ACT_KEY,
    ACT_MODIFIER,
    ACT_LAYER_GOTO,
    ACT_LAYER_TOGGLE
} ActionType;
 
typedef struct {
    ActionType type;
    uint8_t    value;
} KeyAction;
 
KeyAction layout[MAX_LAYERS][MAX_KEYS];
uint8_t   current_layer = 0;
 
volatile bool config_updating = false;
static char   json_buf[FLASH_SECTOR_SIZE];
 
#define NUM_COLS 8
#define NUM_ROWS 8
const uint8_t col_pins[NUM_COLS] = {2, 3, 4, 5, 6, 7, 8, 9};
const uint8_t row_pins[NUM_ROWS] = {10, 11, 12, 13, 14, 15, 16, 17};
 
static void key_scanning_task(void *data);
static void usb_device_task(void *data);
static void cdc_config_task(void *data);
 
KeyAction parse_string_to_action(char* str) {
    KeyAction act = {ACT_NONE, 0};
    if (!str || strlen(str) == 0) return act;
 
    // Layer switching commands
    if (strncmp(str, "GOTO ", 5) == 0) {
        act.type  = ACT_LAYER_GOTO;
        act.value = atoi(str + 5);
        return act;
    }
    if (strncmp(str, "TOGGLE ", 7) == 0) {
        act.type  = ACT_LAYER_TOGGLE;
        act.value = atoi(str + 7);
        return act;
    }
 
    // Modifiers
    if (strcmp(str, "Shift") == 0) return (KeyAction){ACT_MODIFIER, KEYBOARD_MODIFIER_LEFTSHIFT};
    if (strcmp(str, "Ctrl")  == 0) return (KeyAction){ACT_MODIFIER, KEYBOARD_MODIFIER_LEFTCTRL};
    if (strcmp(str, "Alt")   == 0) return (KeyAction){ACT_MODIFIER, KEYBOARD_MODIFIER_LEFTALT};
    if (strcmp(str, "Win")   == 0) return (KeyAction){ACT_MODIFIER, KEYBOARD_MODIFIER_LEFTGUI};
 
    // Named keys
    if (strcmp(str, "Enter")     == 0) return (KeyAction){ACT_KEY, HID_KEY_ENTER};
    if (strcmp(str, "Backspace") == 0) return (KeyAction){ACT_KEY, HID_KEY_BACKSPACE};
    if (strcmp(str, "Tab")       == 0) return (KeyAction){ACT_KEY, HID_KEY_TAB};
    if (strcmp(str, "Caps Lock") == 0) return (KeyAction){ACT_KEY, HID_KEY_CAPS_LOCK};
    if (strcmp(str, " ")         == 0) return (KeyAction){ACT_KEY, HID_KEY_SPACE};
 
    // Letters A-Z
    if (strlen(str) == 1 && str[0] >= 'A' && str[0] <= 'Z') {
        return (KeyAction){ACT_KEY, HID_KEY_A + (str[0] - 'A')};
    }
 
    // Punctuation & numbers
    char last = str[strlen(str) - 1];
    if (last >= '1' && last <= '9') return (KeyAction){ACT_KEY, HID_KEY_1 + (last - '1')};
    if (last == '0')  return (KeyAction){ACT_KEY, HID_KEY_0};
    if (last == '`')  return (KeyAction){ACT_KEY, HID_KEY_GRAVE};
    if (last == '-')  return (KeyAction){ACT_KEY, HID_KEY_MINUS};
    if (last == '=')  return (KeyAction){ACT_KEY, HID_KEY_EQUAL};
    if (last == '[')  return (KeyAction){ACT_KEY, HID_KEY_BRACKET_LEFT};
    if (last == ']')  return (KeyAction){ACT_KEY, HID_KEY_BRACKET_RIGHT};
    if (last == '\\') return (KeyAction){ACT_KEY, HID_KEY_BACKSLASH};
    if (last == ';')  return (KeyAction){ACT_KEY, HID_KEY_SEMICOLON};
    if (last == '\'') return (KeyAction){ACT_KEY, HID_KEY_APOSTROPHE};
    if (last == ',')  return (KeyAction){ACT_KEY, HID_KEY_COMMA};
    if (last == '.')  return (KeyAction){ACT_KEY, HID_KEY_PERIOD};
    if (last == '/')  return (KeyAction){ACT_KEY, HID_KEY_SLASH};
 
    return act;
}
 
void parse_json_to_ram() {
    memset(layout, 0, sizeof(layout));
 
    memcpy(json_buf, flash_target_contents, FLASH_SECTOR_SIZE);
    json_buf[FLASH_SECTOR_SIZE - 1] = '\0';
 
    if (json_buf[0] != '{') return; // Not valid JSON
 
    cJSON *root = cJSON_Parse(json_buf);
    if (!root) return;
 
    for (int layer = 0; layer < MAX_LAYERS; layer++) {
        char layer_key[4];
        snprintf(layer_key, sizeof(layer_key), "%d", layer);
 
        cJSON *layer_array = cJSON_GetObjectItemCaseSensitive(root, layer_key);
        if (!cJSON_IsArray(layer_array)) continue;
 
        int key_count = cJSON_GetArraySize(layer_array);
        if (key_count > MAX_KEYS) key_count = MAX_KEYS;
 
        for (int k = 0; k < key_count; k++) {
            cJSON *item = cJSON_GetArrayItem(layer_array, k);
            if (!cJSON_IsString(item)) continue;
            layout[layer][k] = parse_string_to_action(item->valuestring);
        }
    }
 
    cJSON_Delete(root);
}
 
static void send_hid_key(uint8_t modifier, uint8_t keycode) {
    if (!tud_hid_ready()) return;
 
    nkro_report_t report = {0};
    report.modifier = modifier;
    nkro_set_key(&report, keycode);
 
    tud_hid_report(0, &report, NKRO_REPORT_SIZE);
    vTaskDelay(pdMS_TO_TICKS(15));
 
    memset(&report, 0, sizeof(report));
    tud_hid_report(0, &report, NKRO_REPORT_SIZE);
    vTaskDelay(pdMS_TO_TICKS(15));
}
 
void init_matrix() {
    for (int i = 0; i < NUM_COLS; i++) {
        gpio_init(col_pins[i]);
        gpio_set_dir(col_pins[i], GPIO_IN);
        gpio_pull_down(col_pins[i]);
    }
    for (int i = 0; i < NUM_ROWS; i++) {
        gpio_init(row_pins[i]);
        gpio_set_dir(row_pins[i], GPIO_OUT);
        gpio_put(row_pins[i], 0);
    }
}
 
int main() {
    stdio_init_all();
 
    if (cyw43_arch_init()) {
        printf("Wi-Fi / CYW43 init failed\n");
        return -1;
    }
 
    init_matrix();
    underglow_init();
 
    xTaskCreate(underglow_task,      "underglow_task",    configMINIMAL_STACK_SIZE * 4, NULL, UNDERGLOW_TASK_PRIORITY,    NULL);
    xTaskCreate(cdc_config_task,     "cdc_task",          configMINIMAL_STACK_SIZE * 4, NULL, CDC_TASK_PRIORITY,          NULL);
    xTaskCreate(usb_device_task,     "usb_task",          configMINIMAL_STACK_SIZE * 2, NULL, USB_TASK_PRIORITY,          NULL);
    xTaskCreate(key_scanning_task,   "key_scanning_task", configMINIMAL_STACK_SIZE * 4, NULL, KEY_SCANNING_TASK_PRIORITY, NULL);
 
    vTaskStartScheduler();
    panic_unsupported();
}
 
void usb_device_task(void *data) {
    (void)data;
    tud_init(0);
    for (;;) {
        tud_task();
        taskYIELD();
    }
}
 
void cdc_config_task(void *data) {
    (void)data;
    char rx_buffer[130];
 
    static char     config_buffer[FLASH_SECTOR_SIZE];
    static uint32_t config_len      = 0;
    static bool     receiving_config = false;
 
    for (;;) {
        if (tud_cdc_available()) {
            uint32_t count = tud_cdc_read(rx_buffer, sizeof(rx_buffer) - 1);
            rx_buffer[count] = '\0';
 
            if (strstr(rx_buffer, "CONFIG_BEGIN")) {
                config_updating  = true;
                receiving_config = true;
                config_len       = 0;
                memset(config_buffer, 0, sizeof(config_buffer));
                tud_cdc_write_str("Started receiving config...\r\n");
                tud_cdc_write_flush();
            }
            else if (strstr(rx_buffer, "CONFIG_END")) {
                receiving_config = false;
                tud_cdc_write_str("Config received. Writing to Flash...\r\n");
                tud_cdc_write_flush();

                tud_disconnect();
                vTaskDelay(pdMS_TO_TICKS(100));

                {
                    uint32_t interrupts = save_and_disable_interrupts();
                    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
                    uint32_t write_len = ((config_len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
                    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t *)config_buffer, write_len);
                    restore_interrupts(interrupts);
                }

                parse_json_to_ram();
                config_updating = false;

                vTaskDelay(pdMS_TO_TICKS(100));
                tud_connect();

                tud_cdc_write_str("Flash write complete! Saved permanently.\r\n");
                tud_cdc_write_flush();
            }
            else if (strstr(rx_buffer, "READ_CONFIG")) {
                tud_cdc_write_str("\r\n--- STORED CONFIG BEGIN ---\r\n");
                tud_cdc_write_flush();
 
                for (int i = 0; i < FLASH_SECTOR_SIZE; i += 64) {
                    char chunk[65];
                    memcpy(chunk, &flash_target_contents[i], 64);
                    chunk[64] = '\0';
 
                    if (chunk[0] == (char)0xFF || chunk[0] == '\0') break;
 
                    tud_cdc_write_str(chunk);
                    tud_cdc_write_flush();
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
 
                tud_cdc_write_str("\r\n--- STORED CONFIG END ---\r\n");
                tud_cdc_write_flush();
            }
            else if (receiving_config) {
                if (config_len + count < FLASH_SECTOR_SIZE) {
                    memcpy(&config_buffer[config_len], rx_buffer, count);
                    config_len += count;
                }
            }
            else {
                // Brightness control: a plain integer on the serial line
                char *endptr;
                long input_val = strtol(rx_buffer, &endptr, 10);
                if (endptr != rx_buffer && input_val >= 0 && input_val <= 255) {
                    set_brightness((uint8_t)input_val);
                }
            }
        }
 
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
 
int get_switch_index(int row, int col) {
    return row * NUM_COLS + col;
}

static nkro_report_t last_report = {0};
 
void key_scanning_task(void *data) {
    (void)data;
    bool led_state = false;
 
    parse_json_to_ram();
 
    bool      prev_key_state[NUM_ROWS][NUM_COLS]   = {false};
    KeyAction pressed_action[NUM_ROWS][NUM_COLS]   = {0};
 
    uint8_t previous_layer = 0;
    int     toggle_row     = -1;
    int     toggle_col     = -1;
 
    for (;;) {
        if (config_updating) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
 
        nkro_report_t report = {0};
 
        for (int r = 0; r < NUM_ROWS; r++) {
            gpio_put(row_pins[r], 1);
 
            sleep_us(50); 
 
            uint32_t gpio_state = gpio_get_all();
 
            gpio_put(row_pins[r], 0);
 
            for (int c = 0; c < NUM_COLS; c++) {
                const int switch_index = get_switch_index(r, c);
                if (switch_index >= MAX_KEYS) break;
 
                // Extract this specific column's state from the 32-bit snapshot
                bool currently_pressed = (gpio_state & (1ul << col_pins[c])) != 0;
                bool was_pressed       = prev_key_state[r][c];

                if (currently_pressed) {
                    printf("[LOG] Key at row %d, col %d is currently pressed. Action type: %d, value: %d\n",
                           r, c, layout[current_layer][switch_index].type, layout[current_layer][switch_index].value);
                }
 
                if (currently_pressed && !was_pressed) {
                    KeyAction action = layout[current_layer][switch_index];
                    pressed_action[r][c] = action;   
 
                    if (action.type == ACT_LAYER_GOTO) {
                        current_layer = action.value;
                    }
                    else if (action.type == ACT_LAYER_TOGGLE) {
                        if (toggle_row == -1) {       
                            previous_layer = current_layer;
                            current_layer  = action.value;
                            toggle_row     = r;
                            toggle_col     = c;
                        }
                    }
                }
 
                // ── Key-up edge ──
                if (!currently_pressed && was_pressed) {
                    if (r == toggle_row && c == toggle_col) {
                        current_layer = previous_layer;
                        toggle_row    = -1;
                        toggle_col    = -1;
                    }
                    pressed_action[r][c] = (KeyAction){ACT_NONE, 0};
                }
 
                // ── Accumulate held keys into bitmap ──
                if (currently_pressed) {
                    KeyAction action = pressed_action[r][c];
                    if (action.type == ACT_KEY) {
                        nkro_set_key(&report, action.value);
                    }
                    else if (action.type == ACT_MODIFIER) {
                        report.modifier |= action.value;
                    }
                }
 
                prev_key_state[r][c] = currently_pressed;
            }
        }
 
        // Send the HID report if changes are detected
        if (tud_hid_ready()) {
            if (memcmp(&report, &last_report, NKRO_REPORT_SIZE) != 0) {
                tud_hid_report(0, &report, NKRO_REPORT_SIZE);
                last_report = report;
            }
        }
 
        led_state = !led_state;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── TinyUSB HID callbacks ─────────────────────────────────────────────
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t* buffer, uint16_t reqlen) {
    (void) instance; (void) report_id;
    (void) report_type; (void) buffer; (void) reqlen;
    return 0;
}
 
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const* buffer, uint16_t bufsize) {
    (void) instance; (void) report_id;
    (void) report_type; (void) buffer; (void) bufsize;
}
 
// ── FreeRTOS static-memory hooks ─────────────────────────────────────
#if configSUPPORT_STATIC_ALLOCATION
static StaticTask_t idle_task_tcb;
static StackType_t  idle_task_stack[mainIDLE_TASK_STACK_DEPTH];
void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t  **ppxIdleTaskStackBuffer,
    uint32_t      *pulIdleTaskStackSize
) {
    *ppxIdleTaskTCBBuffer   = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize   = mainIDLE_TASK_STACK_DEPTH;
}
 
static StaticTask_t timer_task_tcb;
static StackType_t  timer_task_stack[configMINIMAL_STACK_SIZE];
void vApplicationGetTimerTaskMemory(
    StaticTask_t **ppxTimerTaskTCBBuffer,
    StackType_t  **ppxTimerTaskStackBuffer,
    uint32_t      *pulTimerTaskStackSize
) {
    *ppxTimerTaskTCBBuffer   = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize   = configMINIMAL_STACK_SIZE;
}
#endif
 