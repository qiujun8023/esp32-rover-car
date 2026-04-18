#include "motor.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"

// 10 kHz 可避开可闻噪声,8-bit 对应占空比 0-255
#define MOTOR_PWM_FREQ 10000
#define MOTOR_PWM_RES  8

static const char* TAG = "motor";

// s_left/s_right 会被 ui 任务和 http 任务并发访问,需要互斥
static int               s_left = 0, s_right = 0;
static SemaphoreHandle_t s_speed_mutex = NULL;

// 最近一次 motor_drive 的 tick,用于看门狗判断失联,由 s_speed_mutex 一起保护
static TickType_t s_last_cmd_tick = 0;

typedef struct {
    gpio_num_t     in1;
    gpio_num_t     in2;
    gpio_num_t     pwm;
    ledc_channel_t ch;
} wheel_cfg_t;

static const wheel_cfg_t WHEELS[WHEEL_COUNT] = {
    [WHEEL_FL] = {MOTOR_FL_IN1, MOTOR_FL_IN2, MOTOR_FL_PWM, LEDC_CHANNEL_0},
    [WHEEL_FR] = {MOTOR_FR_IN1, MOTOR_FR_IN2, MOTOR_FR_PWM, LEDC_CHANNEL_1},
    [WHEEL_RL] = {MOTOR_RL_IN1, MOTOR_RL_IN2, MOTOR_RL_PWM, LEDC_CHANNEL_2},
    [WHEEL_RR] = {MOTOR_RR_IN1, MOTOR_RR_IN2, MOTOR_RR_PWM, LEDC_CHANNEL_3},
};

static inline int clamp(int v, int lo, int hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void set_pwm(ledc_channel_t ch, uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

void motor_init(void) {
    // STBY 拉高使 TB6612 退出待机
    uint64_t      stby_mask = (1ULL << MOTOR_STBY_L) | (1ULL << MOTOR_STBY_R);
    gpio_config_t stby_cfg  = {
         .pin_bit_mask = stby_mask,
         .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&stby_cfg);
    gpio_set_level(MOTOR_STBY_L, 1);
    gpio_set_level(MOTOR_STBY_R, 1);

    s_speed_mutex = xSemaphoreCreateMutex();
    configASSERT(s_speed_mutex);

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = MOTOR_PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    for (int i = 0; i < WHEEL_COUNT; i++) {
        const wheel_cfg_t* w = &WHEELS[i];

        uint64_t      dir_mask = (1ULL << w->in1) | (1ULL << w->in2);
        gpio_config_t dir_cfg  = {
             .pin_bit_mask = dir_mask,
             .mode         = GPIO_MODE_OUTPUT,
        };
        gpio_config(&dir_cfg);
        gpio_set_level(w->in1, 0);
        gpio_set_level(w->in2, 0);

        ledc_channel_config_t ch_cfg = {
            .gpio_num   = w->pwm,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = w->ch,
            .timer_sel  = LEDC_TIMER_0,
            .duty       = 0,
            .hpoint     = 0,
        };
        ledc_channel_config(&ch_cfg);
    }

    ESP_LOGI(TAG, "motor ready, pwm=%dhz/%dbit", MOTOR_PWM_FREQ, MOTOR_PWM_RES);
}

void motor_set_wheel(wheel_t wheel, int speed) {
    if (wheel >= WHEEL_COUNT)
        return;

    const wheel_cfg_t* w = &WHEELS[wheel];
    speed                = clamp(speed, -255, 255);

    if (speed > 0) {
        gpio_set_level(w->in1, 1);
        gpio_set_level(w->in2, 0);
    } else if (speed < 0) {
        gpio_set_level(w->in1, 0);
        gpio_set_level(w->in2, 1);
        speed = -speed;
    } else {
        gpio_set_level(w->in1, 0);
        gpio_set_level(w->in2, 0);
    }

    set_pwm(w->ch, (uint32_t)speed);
}

// 左侧电机与右侧镜像安装,必须取反才能保证前进方向一致
void motor_drive(int left, int right) {
    // tick 必须和 speed 在同一把锁里更新,否则 watchdog 可能看到新 speed + 旧 tick 而误触发停车
    xSemaphoreTake(s_speed_mutex, portMAX_DELAY);
    s_left          = left;
    s_right         = right;
    s_last_cmd_tick = xTaskGetTickCount();
    xSemaphoreGive(s_speed_mutex);

    motor_set_wheel(WHEEL_FL, -left);
    motor_set_wheel(WHEEL_RL, -left);
    motor_set_wheel(WHEEL_FR, right);
    motor_set_wheel(WHEEL_RR, right);
}

void motor_stop(void) {
    motor_drive(0, 0);
}

void motor_get_speed(int* left, int* right) {
    xSemaphoreTake(s_speed_mutex, portMAX_DELAY);
    if (left)
        *left = s_left;
    if (right)
        *right = s_right;
    xSemaphoreGive(s_speed_mutex);
}

bool motor_watchdog_check(uint32_t timeout_ms) {
    xSemaphoreTake(s_speed_mutex, portMAX_DELAY);
    int        l         = s_left;
    int        r         = s_right;
    TickType_t last_tick = s_last_cmd_tick;
    xSemaphoreGive(s_speed_mutex);

    if (l == 0 && r == 0)
        return false;

    // 无符号减法天然处理 tick 回绕
    uint32_t delta = (xTaskGetTickCount() - last_tick) * portTICK_PERIOD_MS;
    if (delta >= timeout_ms) {
        ESP_LOGW(TAG, "cmd timeout %u ms, stop", (unsigned)delta);
        motor_stop();
        return true;
    }
    return false;
}