#include <FreeRTOS.h>

#include "leds.h"

// Private variable to hold the current brightness (0-255).
static uint8_t global_brightness = 50; 

void set_brightness(uint8_t b) {
    global_brightness = b;
}

uint32_t sk6812_color(uint8_t r, uint8_t g, uint8_t b) {
    r = (r * global_brightness) / 255;
    g = (g * global_brightness) / 255;
    b = (b * global_brightness) / 255;

    return ((uint32_t)g << 24) |
           ((uint32_t)r << 16) |
           ((uint32_t)b <<  8); 
}

// ------------------------------------------------------------------ //
// PIO state for underglow
// ------------------------------------------------------------------ //
static PIO  led_pio;
static uint led_sm;

void underglow_init(void) {
    led_pio = pio0;
    uint offset = pio_add_program(led_pio, &ws2812_program);
    led_sm = pio_claim_unused_sm(led_pio, true);

    ws2812_program_init(led_pio, led_sm, offset, LED_PIN, LED_FREQ_HZ, false);
}

void put_pixel(uint32_t pixel_grbw) {
    pio_sm_put_blocking(led_pio, led_sm, pixel_grbw);
}

void show_leds(uint32_t *pixels, uint count) {
    uint32_t irq_state = save_and_disable_interrupts();
    for (uint i = 0; i < count; i++) {
        put_pixel(pixels[i]);
    }
    sleep_us(100);
    restore_interrupts(irq_state);
}

// ------------------------------------------------------------------ //
// hue: 0–255
// ------------------------------------------------------------------ //
uint32_t wheel(uint8_t hue) {
    uint8_t r, g, b;
    if (hue < 85) {
        r = hue * 3;
        g = 255 - hue * 3;
        b = 0;
    } else if (hue < 170) {
        hue -= 85;
        r = 255 - hue * 3;
        g = 0;
        b = hue * 3;
    } else {
        hue -= 170;
        r = 0;
        g = hue * 3;
        b = 255 - hue * 3;
    }
    
    return sk6812_color(r, g, b);
}

void underglow_task(void *data) {
    (void)data;

    uint32_t pixels[LED_COUNT];
    uint8_t  hue_offset = 0;

    for (;;) {
        for (uint i = 0; i < LED_COUNT; i++) {
            uint8_t hue = hue_offset + (uint8_t)((i * 256) / LED_COUNT);
            pixels[i] = wheel(hue);
        }
        show_leds(pixels, 61);

        hue_offset++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    vTaskDelete(NULL);
}
