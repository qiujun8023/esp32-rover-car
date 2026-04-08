#include "motor.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>

#include "config.h"

// pwm 配置
#define MOTOR_PWM_FREQ 10000  // pwm 频率 10khz
#define MOTOR_PWM_RES  8      // pwm 分辨率 8-bit, 占空比 0-255

static const char* TAG = "motor";

// 记录当前左右轮速度状态
static volatile int s_left = 0, s_right = 0;

typedef struct {
    gpio_num_t     in1;
    gpio_num_t     in2;
    gpio_num_t     pwm;
    ledc_channel_t ch;
} wheel_cfg_t;

/**
 * 电机配置说明:
 *
 * 使用两片 tb6612 驱动芯片，左板控制 fl+rl，右板控制 fr+rr。
 * 每片 tb6612 有独立的 stby 引脚用于待机控制。
 *
 * 电机方向:
 * - in1=1, in2=0: 正转 (前进)
 * - in1=0, in2=1: 反转 (后退)
 * - in1=0, in2=0: 停止 (滑行)
 *
 * 由于左右电机镜像安装，逻辑控制在 motor_drive 中统一处理。
 */
static const wheel_cfg_t WHEELS[WHEEL_COUNT] = {
    [WHEEL_FL] = {MOTOR_FL_IN1, MOTOR_FL_IN2, MOTOR_FL_PWM, LEDC_CHANNEL_0},
    [WHEEL_FR] = {MOTOR_FR_IN1, MOTOR_FR_IN2, MOTOR_FR_PWM, LEDC_CHANNEL_1},
    [WHEEL_RL] = {MOTOR_RL_IN1, MOTOR_RL_IN2, MOTOR_RL_PWM, LEDC_CHANNEL_2},
    [WHEEL_RR] = {MOTOR_RR_IN1, MOTOR_RR_IN2, MOTOR_RR_PWM, LEDC_CHANNEL_3},
};

static inline int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void set_pwm(ledc_channel_t ch, uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

void motor_init(void) {
    // 配置 stby 引脚并拉高 (退出待机模式)
    uint64_t      stby_mask = (1ULL << MOTOR_STBY_L) | (1ULL << MOTOR_STBY_R);
    gpio_config_t stby_cfg  = {
         .pin_bit_mask = stby_mask,
         .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&stby_cfg);
    gpio_set_level(MOTOR_STBY_L, 1);
    gpio_set_level(MOTOR_STBY_R, 1);

    // 配置 pwm 定时器
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = MOTOR_PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    // 初始化所有轮子的方向引脚和 pwm 通道
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

    ESP_LOGI(TAG, "motor init ok, freq=%d, res=%d", MOTOR_PWM_FREQ, MOTOR_PWM_RES);
}

void motor_set_wheel(wheel_t wheel, int speed) {
    if (wheel >= WHEEL_COUNT) return;

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

/**
 * 差速驱动逻辑
 *
 * left/right 范围: [-255, 255]
 * 正值前进，负值后退。
 * 由于左侧电机反向安装，在此处进行物理补偿。
 */
void motor_drive(int left, int right) {
    s_left  = left;
    s_right = right;

    // 左侧轮子速度取反以实现前进方向一致
    motor_set_wheel(WHEEL_FL, -left);
    motor_set_wheel(WHEEL_RL, -left);

    // 右侧轮子保持原始极性
    motor_set_wheel(WHEEL_FR, right);
    motor_set_wheel(WHEEL_RR, right);
}

void motor_stop(void) {
    motor_drive(0, 0);
}

void motor_get_speed(int* left, int* right) {
    if (left) *left = s_left;
    if (right) *right = s_right;
}