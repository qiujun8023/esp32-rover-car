#pragma once
#include <stdint.h>

typedef struct {
    uint8_t r, g, b;
} led_color_t;

#define LED_BLACK  ((led_color_t){0, 0, 0})
#define LED_RED    ((led_color_t){255, 0, 0})
#define LED_GREEN  ((led_color_t){0, 255, 0})
#define LED_BLUE   ((led_color_t){0, 0, 255})
#define LED_WHITE  ((led_color_t){255, 255, 255})
#define LED_YELLOW ((led_color_t){255, 180, 0})
#define LED_CYAN   ((led_color_t){0, 255, 255})

void led_init(void);
void led_set(uint8_t index, led_color_t color);
void led_set_all(led_color_t color);
// set 只写缓冲,需要显式 flush 才会真正推送到硬件
void led_flush(void);
void led_off(void);