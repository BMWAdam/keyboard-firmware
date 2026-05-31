#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

#include "tusb.h"
#include "pico/cyw43_arch.h"

#define LED_PIN       19
#define LED_COUNT     84
#define LED_FREQ_HZ   800000
#define UNDERGLOW_TASK_PRIORITY 1
#define MAX_LAYERS    2

// ── COMPILER & VIRTUAL MACHINE STRUCTURES ──
typedef enum {
    OP_END = 0, OP_PUSH_NUM, OP_PUSH_T, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_SIN
} OpCode;

typedef struct __attribute__((packed)) {
    uint8_t op;
    float value;
} Instruction;

#define MAX_INSTR_PER_COLOR 10

typedef struct {
    Instruction instrs[MAX_INSTR_PER_COLOR];
    uint8_t count;
} CompiledExpr;

typedef struct {
    CompiledExpr red;
    CompiledExpr green;
    CompiledExpr blue;
} DynamicLED;

typedef struct {
    uint8_t brightness;
    DynamicLED leds[LED_COUNT];
} LayerLEDConfig;

extern LayerLEDConfig led_layouts[MAX_LAYERS];
extern uint8_t current_underglow_layer;

uint32_t sk6812_color(uint8_t r, uint8_t g, uint8_t b);
void underglow_init(void);
void show_leds(uint32_t *pixels, uint count);
void underglow_task(void *data);
void set_brightness(uint8_t brightness);

float evaluate_bytecode(CompiledExpr* expr, float current_t);

#endif // LEDS_H
