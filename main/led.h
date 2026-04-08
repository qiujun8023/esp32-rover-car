#pragma once
#include <stdint.h>

// RGB 颜色结构
typedef struct {
    uint8_t r, g, b;
} led_color_t;

// 预定义颜色
#define LED_BLACK  ((led_color_t){0, 0, 0})
#define LED_RED    ((led_color_t){255, 0, 0})
#define LED_GREEN  ((led_color_t){0, 255, 0})
#define LED_BLUE   ((led_color_t){0, 0, 255})
#define LED_WHITE  ((led_color_t){255, 255, 255})
#define LED_YELLOW ((led_color_t){255, 180, 0})
#define LED_CYAN   ((led_color_t){0, 255, 255})

void led_init(void);

// 设置单个灯珠颜色
void led_set(uint8_t index, led_color_t color);

// 设置所有灯珠为同一颜色
void led_set_all(led_color_t color);

// 刷新显示（set 操作后需调用）
void led_flush(void);

// 熄灭所有灯珠
void led_off(void);