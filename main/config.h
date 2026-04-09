#pragma once

#include <driver/gpio.h>

/**
 * gpio 引脚配置
 *
 * 避开特殊功能引脚: 0, 3, 19, 20, 43, 44, 45, 46
 * gpio 48 已被板载 ws2812 占用
 */

// 电机驱动引脚 (两片 tb6612)
#define MOTOR_RL_PWM GPIO_NUM_4   // 后左 pwm
#define MOTOR_RL_IN2 GPIO_NUM_5   // 后左 in2
#define MOTOR_RL_IN1 GPIO_NUM_6   // 后左 in1
#define MOTOR_STBY_L GPIO_NUM_7   // 左侧待机使能
#define MOTOR_FL_IN1 GPIO_NUM_15  // 前左 in1
#define MOTOR_FL_IN2 GPIO_NUM_16  // 前左 in2
#define MOTOR_FL_PWM GPIO_NUM_17  // 前左 pwm

#define MOTOR_RR_PWM GPIO_NUM_42  // 后右 pwm
#define MOTOR_RR_IN2 GPIO_NUM_41  // 后右 in2
#define MOTOR_RR_IN1 GPIO_NUM_40  // 后右 in1
#define MOTOR_STBY_R GPIO_NUM_39  // 右侧待机使能
#define MOTOR_FR_IN1 GPIO_NUM_38  // 前右 in1
#define MOTOR_FR_IN2 GPIO_NUM_37  // 前右 in2
#define MOTOR_FR_PWM GPIO_NUM_36  // 前右 pwm

// ws2812 灯带配置
#define LED_GPIO  GPIO_NUM_18
#define LED_COUNT 8

// st7789 显示屏配置 (240x240 spi)
#define DISPLAY_SCLK   GPIO_NUM_9
#define DISPLAY_MOSI   GPIO_NUM_10
#define DISPLAY_CS     GPIO_NUM_11
#define DISPLAY_DC     GPIO_NUM_12
#define DISPLAY_RST    GPIO_NUM_13
#define DISPLAY_BLK    GPIO_NUM_14
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 240

// i2s 音频引脚 (预留，全双工模式)
// 全双工 i2s 中 BCLK 和 WS 由扬声器与麦克风共享，仅数据线独立
#define I2S_SPK_BCLK GPIO_NUM_47  // 与 I2S_MIC_SCK 共用
#define I2S_SPK_LRCK GPIO_NUM_21  // 与 I2S_MIC_WS 共用
#define I2S_SPK_DOUT GPIO_NUM_35
#define I2S_MIC_SCK  GPIO_NUM_47  // 与 I2S_SPK_BCLK 共用
#define I2S_MIC_WS   GPIO_NUM_21  // 与 I2S_SPK_LRCK 共用
#define I2S_MIC_DIN  GPIO_NUM_2

// wifi 配置
#define WIFI_AP_SSID "esp32-rover-car"