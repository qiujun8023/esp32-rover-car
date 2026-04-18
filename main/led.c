#include "led.h"

#include <esp_log.h>
#include <led_strip.h>

#include "config.h"

// ws2812 位时序需要 10mhz rmt 分辨率才能满足 T0H/T1H 约束
#define LED_RMT_FREQ_HZ (10 * 1000 * 1000)

static const char*        TAG = "led";
static led_strip_handle_t s_strip;

void led_init(void) {
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = LED_GPIO,
        .max_leds         = LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model        = LED_MODEL_WS2812,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = LED_RMT_FREQ_HZ,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);

    ESP_LOGI(TAG, "led ready, pixels=%d", LED_COUNT);
}

void led_set(uint8_t index, led_color_t color) {
    if (index < LED_COUNT) {
        led_strip_set_pixel(s_strip, index, color.r, color.g, color.b);
    }
}

void led_set_all(led_color_t color) {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel(s_strip, i, color.r, color.g, color.b);
    }
}

void led_flush(void) {
    led_strip_refresh(s_strip);
}

void led_off(void) {
    // clear 只清缓冲,需要 refresh 才真正熄灭硬件
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);
}