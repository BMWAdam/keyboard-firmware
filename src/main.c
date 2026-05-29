#include <stdio.h>
#include <stdlib.h>
#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include <ctype.h>

#include "tusb.h"
#include "pico/cyw43_arch.h"
#include "leds.h"

#define MY_TASK_PRIORITY        2
#define USB_TASK_PRIORITY       3

static void key_scanning_task(void *data);
static void usb_device_task(void *data);

static void send_hid_key(uint8_t modifier, uint8_t keycode) {
    if (!tud_hid_ready()) return;
    uint8_t keycodes[6] = { keycode, 0, 0, 0, 0, 0 };
    tud_hid_keyboard_report(0, modifier, keycodes);
    vTaskDelay(pdMS_TO_TICKS(15)); 
    tud_hid_keyboard_report(0, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(15));
}

int main() {
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("Wi-Fi / CYW43 init failed\n");
        return -1;
    }

    underglow_init();

    xTaskCreate(usb_device_task, "usb_task", configMINIMAL_STACK_SIZE * 2, NULL, USB_TASK_PRIORITY, NULL);
    xTaskCreate(key_scanning_task, "application_task", configMINIMAL_STACK_SIZE * 2, NULL, MY_TASK_PRIORITY, NULL);
    xTaskCreate(underglow_task,    "underglow_task",   configMINIMAL_STACK_SIZE * 2, NULL, UNDERGLOW_TASK_PRIORITY, NULL);

    vTaskStartScheduler();
    // we should never return from FreeRTOS
    panic_unsupported();
}

void usb_device_task(void *data) {
    (void)data;
    tud_init(0);
    
    for (;;) {
        tud_task();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void key_scanning_task(void *data) {
    (void)data;
    bool led_state = false;
    char rx_buffer[64];

    printf("user task started\n");

    for (;;) {
        if (tud_cdc_available()) {
            uint32_t count = tud_cdc_read(rx_buffer, sizeof(rx_buffer) - 1);
            rx_buffer[count] = '\0';
            
            printf("Received: %s\n", rx_buffer);

            // 1. Attempt to parse input as an integer
            char *endptr;
            long input_val = strtol(rx_buffer, &endptr, 10);
            
            // 2. Check if the input is genuinely a number (allow trailing \r, \n, or spaces)
            bool is_valid_number = (endptr != rx_buffer);
            if (is_valid_number) {
                for (char *p = endptr; *p != '\0'; p++) {
                    if (!isspace((unsigned char)*p)) {
                        is_valid_number = false;
                        break;
                    }
                }
            }

            // 3. If it's a number between 0 and 255, set brightness
            if (is_valid_number && input_val >= 0 && input_val <= 255) {
                char msg[64];
                int msg_len = snprintf(msg, sizeof(msg), "Brightness set to: %ld\r\n", input_val);
                tud_cdc_write(msg, msg_len);
                tud_cdc_write_flush();
                
                set_brightness((uint8_t)input_val);
            } 
            // 4. Otherwise, treat it as a typing string
            else {
                tud_cdc_write_str("Typing: ");
                tud_cdc_write(rx_buffer, count);
                tud_cdc_write_str("\r\n");
                tud_cdc_write_flush();

                for (uint32_t i = 0; i < count; i++) {
                    char c = rx_buffer[i];
                    uint8_t modifier = 0;
                    uint8_t keycode = 0;

                    if (c >= 'a' && c <= 'z') {
                        keycode = HID_KEY_A + (c - 'a');
                    } 
                    else if (c >= 'A' && c <= 'Z') {
                        keycode = HID_KEY_A + (c - 'A');
                        modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
                    } 
                    else if (c >= '1' && c <= '9') {
                        keycode = HID_KEY_1 + (c - '1');
                    } 
                    else if (c == '0') {
                        keycode = HID_KEY_0;
                    } 
                    else if (c == ' ') {
                        keycode = HID_KEY_SPACE;
                    } 
                    else if (c == '\r' || c == '\n') {
                        keycode = HID_KEY_ENTER;
                    }
                    // TODO (Add more symbols like punctuation here if needed)

                    // If we found a valid mapping, send it
                    if (keycode != 0) {
                        //send_hid_key(modifier, keycode);
                    }
                }
            }
        }

        led_state = !led_state;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    vTaskDelete(NULL);
}

// --------------------------------------------------------------------+
// TinyUSB HID Callbacks
// --------------------------------------------------------------------+
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0; 
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

/*
* These functions are requried for FreeRTOS to work in static memory mode.
*/

#if configSUPPORT_STATIC_ALLOCATION
static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[mainIDLE_TASK_STACK_DEPTH];
void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    uint32_t *pulIdleTaskStackSize
) {
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = mainIDLE_TASK_STACK_DEPTH;
}

static StaticTask_t timer_task_tcb;
static StackType_t timer_task_stack[configMINIMAL_STACK_SIZE];
void vApplicationGetTimerTaskMemory(
    StaticTask_t **ppxTimerTaskTCBBuffer,
    StackType_t **ppxTimerTaskStackBuffer,
    uint32_t *pulTimerTaskStackSize
) {
    *ppxTimerTaskTCBBuffer = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize = configMINIMAL_STACK_SIZE;
}
#endif
