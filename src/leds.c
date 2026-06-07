#include <FreeRTOS.h>
#include <task.h>
#include <string.h>
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "leds.h"

#define UNDERGLOW_BRIGHTNESS 254  // dim white, out of 255

static TaskHandle_t underglow_task_handle = NULL;

volatile bool underglow_enabled = true;

uint32_t sk6812_color(uint8_t r, uint8_t g, uint8_t b) {
    r = (r * UNDERGLOW_BRIGHTNESS) / 255;
    g = (g * UNDERGLOW_BRIGHTNESS) / 255;
    b = (b * UNDERGLOW_BRIGHTNESS) / 255;

    return ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8);
}

static PIO  led_pio;
static uint led_sm;
static int  dma_chan;

void dma_isr() {
    dma_hw->ints0 = 1u << dma_chan;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (underglow_task_handle != NULL) {
        vTaskNotifyGiveFromISR(underglow_task_handle, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void underglow_init(void) {
    led_pio = pio0;
    uint offset = pio_add_program(led_pio, &ws2812_program);
    led_sm = pio_claim_unused_sm(led_pio, true);

    ws2812_program_init(led_pio, led_sm, offset, LED_PIN, LED_FREQ_HZ, false);
    gpio_disable_pulls(LED_PIN);

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);

    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(led_pio, led_sm, true));

    dma_channel_configure(
        dma_chan, &c, &led_pio->txf[led_sm], NULL, 0, false
    );

    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_isr);
    irq_set_enabled(DMA_IRQ_0, true);
}

void show_leds(uint32_t *pixels, uint count) {
    dma_channel_transfer_from_buffer_now(dma_chan, pixels, count);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(1));
}

extern volatile bool config_updating;
static uint32_t pixels[LED_COUNT];

void underglow_task(void *data) {
    (void)data;
    underglow_task_handle = xTaskGetCurrentTaskHandle();

    static const uint32_t white_pixel = 0;  // computed once below
    uint32_t dim_white;

    // Pre-compute dim white in SK6812 GRB format
    {
        uint8_t v = UNDERGLOW_BRIGHTNESS;
        dim_white = ((uint32_t)v << 24) | ((uint32_t)v << 16) | ((uint32_t)v << 8);
    }

    for (;;) {
        if (config_updating) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!underglow_enabled) {
            memset(pixels, 0, sizeof(pixels));
        } else {
            for (uint i = 0; i < LED_COUNT; i++) {
                pixels[i] = dim_white;
            }
        }

        show_leds(pixels, LED_COUNT);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
