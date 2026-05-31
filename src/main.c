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
 
#define MAX_KEYS   61

// Separate Flash sectors for Layouts and Underglow configurations
#define FLASH_TARGET_OFFSET     (1024 * 1024)
#define FLASH_UNDERGLOW_OFFSET  (FLASH_TARGET_OFFSET + FLASH_SECTOR_SIZE)

const uint8_t *flash_target_contents    = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
const uint8_t *flash_underglow_contents = (const uint8_t *) (XIP_BASE + FLASH_UNDERGLOW_OFFSET);
 
#define NKRO_KEYCODE_BYTES  16
#define NKRO_REPORT_SIZE    (1 + 1 + NKRO_KEYCODE_BYTES)
 
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[NKRO_KEYCODE_BYTES];
} nkro_report_t;
 
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

// ── COMPILER IMPLEMENTATION ──
static const char* parse_ptr;

static void skip_whitespace() {
    while (*parse_ptr == ' ' || *parse_ptr == '\t' || *parse_ptr == '\r' || *parse_ptr == '\n') parse_ptr++;
}

static void emit(CompiledExpr* out, OpCode op, float val) {
    if (out->count < MAX_INSTR_PER_COLOR) {
        out->instrs[out->count].op = op;
        out->instrs[out->count].value = val;
        out->count++;
    }
}

static void parse_expression(CompiledExpr* out);

static void parse_factor(CompiledExpr* out) {
    skip_whitespace();
    if (strncmp(parse_ptr, "SIN", 3) == 0) {
        parse_ptr += 3;
        skip_whitespace();
        if (*parse_ptr == '(') {
            parse_ptr++;
            parse_expression(out);
            skip_whitespace();
            if (*parse_ptr == ')') parse_ptr++;
        }
        emit(out, OP_SIN, 0);
    } else if (*parse_ptr == 'T') {
        parse_ptr++;
        emit(out, OP_PUSH_T, 0);
    } else if (strncmp(parse_ptr, "PI", 2) == 0) {
        parse_ptr += 2;
        emit(out, OP_PUSH_NUM, 3.14159265f);
    } else if (*parse_ptr == '(') {
        parse_ptr++;
        parse_expression(out);
        skip_whitespace();
        if (*parse_ptr == ')') parse_ptr++;
    } else {
        char* end;
        float val = strtof(parse_ptr, &end);
        if (parse_ptr != end) {
            emit(out, OP_PUSH_NUM, val);
            parse_ptr = end;
        }
    }
}

static void parse_term(CompiledExpr* out) {
    parse_factor(out);
    skip_whitespace();
    while (*parse_ptr == '*' || *parse_ptr == '/') {
        char op = *parse_ptr++;
        parse_factor(out);
        if (op == '*') emit(out, OP_MUL, 0);
        else           emit(out, OP_DIV, 0);
        skip_whitespace();
    }
}

static void parse_expression(CompiledExpr* out) {
    parse_term(out);
    skip_whitespace();
    while (*parse_ptr == '+' || *parse_ptr == '-') {
        char op = *parse_ptr++;
        parse_term(out);
        if (op == '+') emit(out, OP_ADD, 0);
        else           emit(out, OP_SUB, 0);
        skip_whitespace();
    }
}

void compile_formula(const char* formula, CompiledExpr* out) {
    out->count = 0;
    if (!formula || strlen(formula) == 0) return;
    parse_ptr = formula;
    parse_expression(out);
}
 
KeyAction parse_string_to_action(char* str) {
    KeyAction act = {ACT_NONE, 0};
    if (!str || strlen(str) == 0) return act;
 
    if (strncmp(str, "GOTO ", 5) == 0) return (KeyAction){ACT_LAYER_GOTO, atoi(str + 5)};
    if (strncmp(str, "TOGGLE ", 7) == 0) return (KeyAction){ACT_LAYER_TOGGLE, atoi(str + 7)};
 
    if (strcmp(str, "Shift") == 0) return (KeyAction){ACT_MODIFIER, KEYBOARD_MODIFIER_LEFTSHIFT};
    if (strcmp(str, "Ctrl")  == 0) return (KeyAction){ACT_MODIFIER, KEYBOARD_MODIFIER_LEFTCTRL};
    if (strcmp(str, "Alt")   == 0) return (KeyAction){ACT_MODIFIER, KEYBOARD_MODIFIER_LEFTALT};
    if (strcmp(str, "Win")   == 0) return (KeyAction){ACT_MODIFIER, KEYBOARD_MODIFIER_LEFTGUI};
 
    if (strcmp(str, "Enter")     == 0) return (KeyAction){ACT_KEY, HID_KEY_ENTER};
    if (strcmp(str, "Backspace") == 0) return (KeyAction){ACT_KEY, HID_KEY_BACKSPACE};
    if (strcmp(str, "Tab")       == 0) return (KeyAction){ACT_KEY, HID_KEY_TAB};
    if (strcmp(str, "Caps Lock") == 0) return (KeyAction){ACT_KEY, HID_KEY_CAPS_LOCK};
    if (strcmp(str, " ")         == 0) return (KeyAction){ACT_KEY, HID_KEY_SPACE};
 
    if (strlen(str) == 1 && str[0] >= 'A' && str[0] <= 'Z') return (KeyAction){ACT_KEY, HID_KEY_A + (str[0] - 'A')};
 
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
 
    if (json_buf[0] != '{') return;
 
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

void parse_underglow_json_to_ram() {
    memset(led_layouts, 0, sizeof(led_layouts));
    
    char u_json_buf[FLASH_SECTOR_SIZE];
    memcpy(u_json_buf, flash_underglow_contents, FLASH_SECTOR_SIZE);
    u_json_buf[FLASH_SECTOR_SIZE - 1] = '\0';

    if (u_json_buf[0] != '{') {
        // FALLBACK: If there is no config, go all white with default brightness
        for (int layer = 0; layer < MAX_LAYERS; layer++) {
            led_layouts[layer].brightness = 10; // Default brightness

            for (int k = 0; k < LED_COUNT; k++) {
                // Hardcode bytecode to push 255.0f for Red, Green, and Blue
                led_layouts[layer].leds[k].red.count = 1;
                led_layouts[layer].leds[k].red.instrs[0] = (Instruction){OP_PUSH_NUM, 255.0f};

                led_layouts[layer].leds[k].green.count = 1;
                led_layouts[layer].leds[k].green.instrs[0] = (Instruction){OP_PUSH_NUM, 255.0f};

                led_layouts[layer].leds[k].blue.count = 1;
                led_layouts[layer].leds[k].blue.instrs[0] = (Instruction){OP_PUSH_NUM, 255.0f};
            }
        }
        return;
    }

    cJSON *root = cJSON_Parse(u_json_buf);
    if (!root) return;

    for (int layer = 0; layer < MAX_LAYERS; layer++) {
        char layer_key[4];
        snprintf(layer_key, sizeof(layer_key), "%d", layer);
        
        cJSON *layer_obj = cJSON_GetObjectItemCaseSensitive(root, layer_key);
        if (!cJSON_IsObject(layer_obj)) continue;

        cJSON *brightness_item = cJSON_GetObjectItemCaseSensitive(layer_obj, "brightness");
        led_layouts[layer].brightness = cJSON_IsNumber(brightness_item) ? brightness_item->valueint : 10;

        cJSON *leds_array = cJSON_GetObjectItemCaseSensitive(layer_obj, "leds");
        if (cJSON_IsArray(leds_array)) {
            int led_count = cJSON_GetArraySize(leds_array);
            if (led_count > LED_COUNT) led_count = LED_COUNT;

            for (int k = 0; k < led_count; k++) {
                cJSON *led_item = cJSON_GetArrayItem(leds_array, k);
                if (cJSON_IsObject(led_item)) {
                    cJSON *r_val = cJSON_GetObjectItemCaseSensitive(led_item, "red");
                    cJSON *g_val = cJSON_GetObjectItemCaseSensitive(led_item, "green");
                    cJSON *b_val = cJSON_GetObjectItemCaseSensitive(led_item, "blue");

                    if (cJSON_IsString(r_val)) compile_formula(r_val->valuestring, &led_layouts[layer].leds[k].red);
                    if (cJSON_IsString(g_val)) compile_formula(g_val->valuestring, &led_layouts[layer].leds[k].green);
                    if (cJSON_IsString(b_val)) compile_formula(b_val->valuestring, &led_layouts[layer].leds[k].blue);
                }
            }
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
    static uint32_t config_len              = 0;
    static bool     receiving_layout_cfg    = false;
    static bool     receiving_underglow_cfg = false;
 
    for (;;) {
        if (tud_cdc_available()) {
            uint32_t count = tud_cdc_read(rx_buffer, sizeof(rx_buffer) - 1);
            rx_buffer[count] = '\0';
 
            // Layout Config Logic
            if (strstr(rx_buffer, "CONFIG_BEGIN")) {
                config_updating      = true;
                receiving_layout_cfg = true;
                config_len           = 0;
                memset(config_buffer, 0, sizeof(config_buffer));
                tud_cdc_write_str("Started receiving KEYMAP config...\r\n");
                tud_cdc_write_flush();
            }
            else if (strstr(rx_buffer, "CONFIG_END")) {
                receiving_layout_cfg = false;
                tud_cdc_write_str("Keymap Config received. Writing to Flash...\r\n");
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

                tud_cdc_write_str("Keymap Flash write complete!\r\n");
                tud_cdc_write_flush();
            }
            
            // Underglow Config Logic
            else if (strstr(rx_buffer, "UNDERGLOW_CONFIG_BEGIN")) {
                config_updating         = true;
                receiving_underglow_cfg = true;
                config_len              = 0;
                memset(config_buffer, 0, sizeof(config_buffer));
                tud_cdc_write_str("Started receiving UNDERGLOW config...\r\n");
                tud_cdc_write_flush();
            }
            else if (strstr(rx_buffer, "UNDERGLOW_CONFIG_END")) {
                receiving_underglow_cfg = false;
                tud_cdc_write_str("Underglow Config received. Writing to Flash...\r\n");
                tud_cdc_write_flush();

                tud_disconnect();
                vTaskDelay(pdMS_TO_TICKS(100));

                {
                    uint32_t interrupts = save_and_disable_interrupts();
                    flash_range_erase(FLASH_UNDERGLOW_OFFSET, FLASH_SECTOR_SIZE);
                    uint32_t write_len = ((config_len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
                    flash_range_program(FLASH_UNDERGLOW_OFFSET, (const uint8_t *)config_buffer, write_len);
                    restore_interrupts(interrupts);
                }

                parse_underglow_json_to_ram();
                config_updating = false;

                vTaskDelay(pdMS_TO_TICKS(100));
                tud_connect();

                tud_cdc_write_str("Underglow Flash write complete!\r\n");
                tud_cdc_write_flush();
            }

            // Read Layout
            else if (strstr(rx_buffer, "READ_CONFIG")) {
                tud_cdc_write_str("\r\n--- STORED KEYMAP CONFIG BEGIN ---\r\n");
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
                tud_cdc_write_str("\r\n--- STORED KEYMAP CONFIG END ---\r\n");
                tud_cdc_write_flush();
            }

            // Read Underglow
            else if (strstr(rx_buffer, "READ_UNDERGLOW_CONFIG")) {
                tud_cdc_write_str("\r\n--- STORED UNDERGLOW CONFIG BEGIN ---\r\n");
                tud_cdc_write_flush();
                for (int i = 0; i < FLASH_SECTOR_SIZE; i += 64) {
                    char chunk[65];
                    memcpy(chunk, &flash_underglow_contents[i], 64);
                    chunk[64] = '\0';
                    if (chunk[0] == (char)0xFF || chunk[0] == '\0') break;
                    tud_cdc_write_str(chunk);
                    tud_cdc_write_flush();
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
                tud_cdc_write_str("\r\n--- STORED UNDERGLOW CONFIG END ---\r\n");
                tud_cdc_write_flush();
            }
            
            // Layer Toggles
            else if (strncmp(rx_buffer, "CHANGE_LAYOUT ", 14) == 0) {
                current_layer = atoi(rx_buffer + 14);
            }
            else if (strncmp(rx_buffer, "CHANGE_UNDERGLOW ", 17) == 0) {
                current_underglow_layer = atoi(rx_buffer + 17);
            }

            else if (strstr(rx_buffer, "UG_VAL_UP")) {
                if (led_layouts[current_underglow_layer].brightness <= 245) {
                    led_layouts[current_underglow_layer].brightness += 10;
                } else {
                    led_layouts[current_underglow_layer].brightness = 255;
                }
                tud_cdc_write_str("Underglow brightness increased.\r\n");
                tud_cdc_write_flush();
            }
            else if (strstr(rx_buffer, "UG_VAL_DOWN")) {
                if (led_layouts[current_underglow_layer].brightness >= 10) {
                    led_layouts[current_underglow_layer].brightness -= 10;
                } else {
                    led_layouts[current_underglow_layer].brightness = 0;
                }
                tud_cdc_write_str("Underglow brightness decreased.\r\n");
                tud_cdc_write_flush();
            }
            else if (strstr(rx_buffer, "UG_TOGGLE")) {
                underglow_enabled = !underglow_enabled;
                tud_cdc_write_str(underglow_enabled ? "Underglow ON.\r\n" : "Underglow OFF.\r\n");
                tud_cdc_write_flush();
            }
            else if (strstr(rx_buffer, "UG_MODE_NEXT")) {
                current_underglow_layer = (current_underglow_layer + 1) % MAX_LAYERS;
                char msg[40];
                snprintf(msg, sizeof(msg), "Underglow mode changed to %d.\r\n", current_underglow_layer);
                tud_cdc_write_str(msg);
                tud_cdc_write_flush();
            }

            // Data Capture
            else if (receiving_layout_cfg || receiving_underglow_cfg) {
                if (config_len + count < FLASH_SECTOR_SIZE) {
                    memcpy(&config_buffer[config_len], rx_buffer, count);
                    config_len += count;
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
 
    // Initial parse on boot
    parse_json_to_ram();
    parse_underglow_json_to_ram();
 
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
 
                bool currently_pressed = (gpio_state & (1ul << col_pins[c])) != 0;
                bool was_pressed       = prev_key_state[r][c];

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
 
                if (!currently_pressed && was_pressed) {
                    if (r == toggle_row && c == toggle_col) {
                        current_layer = previous_layer;
                        toggle_row    = -1;
                        toggle_col    = -1;
                    }
                    pressed_action[r][c] = (KeyAction){ACT_NONE, 0};
                }
 
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
