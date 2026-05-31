#include <FreeRTOS.h>
#include <task.h>
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "leds.h"

static uint8_t global_brightness = 10;
static TaskHandle_t underglow_task_handle = NULL;

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

static PIO  led_pio;
static uint led_sm;
static int  dma_chan;

// --- DMA Interrupt Service Routine ---
void dma_isr() {
    // 1. Clear the interrupt request flag so it doesn't fire continuously
    dma_hw->ints0 = 1u << dma_chan;
    
    // 2. Notify the FreeRTOS task that the transfer is complete
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (underglow_task_handle != NULL) {
        vTaskNotifyGiveFromISR(underglow_task_handle, &xHigherPriorityTaskWoken);
    }
    
    // 3. Force a context switch if a higher priority task was woken
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void underglow_init(void) {
    led_pio = pio0;
    uint offset = pio_add_program(led_pio, &ws2812_program);
    led_sm = pio_claim_unused_sm(led_pio, true);
    
    // 'false' is perfectly correct here for the SK6812 MINI-E (24-bit RGB)
    ws2812_program_init(led_pio, led_sm, offset, LED_PIN, LED_FREQ_HZ, false);

    gpio_disable_pulls(LED_PIN);

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);  
    channel_config_set_write_increment(&c, false); 
    channel_config_set_dreq(&c, pio_get_dreq(led_pio, led_sm, true)); 

    dma_channel_configure(
        dma_chan,
        &c,
        &led_pio->txf[led_sm],
        NULL,
        0,
        false
    );

    // --- Enable Interrupts for DMA ---
    dma_channel_set_irq0_enabled(dma_chan, true);

    irq_set_exclusive_handler(DMA_IRQ_0, dma_isr);
    irq_set_enabled(DMA_IRQ_0, true);
}

void show_leds(uint32_t *pixels, uint count) {
    // Start the DMA transfer in the background
    dma_channel_transfer_from_buffer_now(dma_chan, pixels, count);
    
    // Yield this task (allowing other tasks to run) until the DMA ISR wakes it up
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    
    vTaskDelay(pdMS_TO_TICKS(1));
}

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

uint32_t static_green() {
    return sk6812_color(255, 255, 255);
}

static uint32_t pixels[LED_COUNT];

void underglow_task(void *data) {
    (void)data;

    underglow_task_handle = xTaskGetCurrentTaskHandle();

    uint8_t hue_offset = 0;

    for (;;) {
        for (uint i = 0; i < LED_COUNT; i++) {
            pixels[i] = static_green();
        }
        
        show_leds(pixels, LED_COUNT);

        hue_offset++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    vTaskDelete(NULL);
}
