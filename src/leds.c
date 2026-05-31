#include <FreeRTOS.h>
#include <task.h>
#include <math.h>
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "leds.h"

static uint8_t global_brightness = 10;
static TaskHandle_t underglow_task_handle = NULL;

LayerLEDConfig led_layouts[MAX_LAYERS];
uint8_t current_underglow_layer = 0;

void set_brightness(uint8_t b) {
    global_brightness = b;
}

uint32_t sk6812_color(uint8_t r, uint8_t g, uint8_t b) {
    r = (r * global_brightness) / 255;
    g = (g * global_brightness) / 255;
    b = (b * global_brightness) / 255;

    return ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b <<  8); 
}

#define CALC_STACK_SIZE 8 

float evaluate_bytecode(CompiledExpr* expr, float current_t) {
    if (expr->count == 0) return 0.0f;
    
    float stack[CALC_STACK_SIZE];
    int sp = 0;
    
    for (int i = 0; i < expr->count; i++) {
        Instruction inst = expr->instrs[i];
        switch (inst.op) {
            case OP_PUSH_NUM: if (sp < CALC_STACK_SIZE) stack[sp++] = inst.value; break;
            case OP_PUSH_T:   if (sp < CALC_STACK_SIZE) stack[sp++] = current_t; break;
            case OP_ADD:      if (sp >= 2) { sp--; stack[sp-1] = stack[sp-1] + stack[sp]; } break;
            case OP_SUB:      if (sp >= 2) { sp--; stack[sp-1] = stack[sp-1] - stack[sp]; } break;
            case OP_MUL:      if (sp >= 2) { sp--; stack[sp-1] = stack[sp-1] * stack[sp]; } break;
            case OP_DIV:      
                if (sp >= 2) { 
                    sp--; 
                    stack[sp-1] = (stack[sp] != 0.0f) ? stack[sp-1] / stack[sp] : 0.0f; 
                } 
                break;
            case OP_SIN:      if (sp >= 1) { stack[sp-1] = sinf(stack[sp-1]); } break;
            default: break;
        }
    }
    return (sp > 0) ? stack[0] : 0.0f;
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

    uint64_t boot_time = time_us_64();

    for (;;) {
        if (config_updating) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        float T = (float)(time_us_64() - boot_time) / 1000000.0f;

        uint8_t current_bright = led_layouts[current_underglow_layer].brightness;
        set_brightness(current_bright);

        for (uint i = 0; i < LED_COUNT; i++) {
            DynamicLED* d_led = &led_layouts[current_underglow_layer].leds[i];
            
            uint8_t r = (uint8_t)fmaxf(0.0f, fminf(255.0f, evaluate_bytecode(&d_led->red, T)));
            uint8_t g = (uint8_t)fmaxf(0.0f, fminf(255.0f, evaluate_bytecode(&d_led->green, T)));
            uint8_t b = (uint8_t)fmaxf(0.0f, fminf(255.0f, evaluate_bytecode(&d_led->blue, T)));

            pixels[i] = sk6812_color(r, g, b);
        }
        
        show_leds(pixels, LED_COUNT);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
