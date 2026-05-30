#ifndef LEDS_H
#define LEDS_H

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

#include "tusb.h"
#include "pico/cyw43_arch.h"

#define LED_PIN       19
#define LED_COUNT     61
#define LED_FREQ_HZ   800000
#define UNDERGLOW_TASK_PRIORITY 1

uint32_t sk6812_color(uint8_t r, uint8_t g, uint8_t b);
void underglow_init(void);
void put_pixel(uint32_t pixel_grbw);
void show_leds(uint32_t *pixels, uint count);
uint32_t wheel(uint8_t hue);
void underglow_task(void *data);
void set_brightness(uint8_t brightness);

#endif // LEDS_H
