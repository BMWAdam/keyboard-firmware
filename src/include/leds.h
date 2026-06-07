#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

#include "tusb.h"
#include "pico/cyw43_arch.h"

#define LED_PIN       18
#define LED_COUNT     86
#define LED_FREQ_HZ   800000
#define UNDERGLOW_TASK_PRIORITY 1

extern volatile bool underglow_enabled;

uint32_t sk6812_color(uint8_t r, uint8_t g, uint8_t b);
void underglow_init(void);
void show_leds(uint32_t *pixels, uint count);
void underglow_task(void *data);

#endif // LEDS_H
