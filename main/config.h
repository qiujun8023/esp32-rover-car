#pragma once

#include <driver/gpio.h>

// 避开 esp32-s3 特殊功能引脚: 0, 3, 19, 20, 43, 44, 45, 46;gpio 48 已被板载 ws2812 占用

// 两片 tb6612,左右各一片,独立 stby
#define MOTOR_RL_PWM GPIO_NUM_4
#define MOTOR_RL_IN2 GPIO_NUM_5
#define MOTOR_RL_IN1 GPIO_NUM_6
#define MOTOR_STBY_L GPIO_NUM_7
#define MOTOR_FL_IN1 GPIO_NUM_15
#define MOTOR_FL_IN2 GPIO_NUM_16
#define MOTOR_FL_PWM GPIO_NUM_17

#define MOTOR_RR_PWM GPIO_NUM_42
#define MOTOR_RR_IN2 GPIO_NUM_41
#define MOTOR_RR_IN1 GPIO_NUM_40
#define MOTOR_STBY_R GPIO_NUM_39
#define MOTOR_FR_IN1 GPIO_NUM_38
#define MOTOR_FR_IN2 GPIO_NUM_37
#define MOTOR_FR_PWM GPIO_NUM_36

#define LED_GPIO  GPIO_NUM_18
#define LED_COUNT 8

// st7789 240x240 spi
#define DISPLAY_SCLK   GPIO_NUM_9
#define DISPLAY_MOSI   GPIO_NUM_10
#define DISPLAY_CS     GPIO_NUM_11
#define DISPLAY_DC     GPIO_NUM_12
#define DISPLAY_RST    GPIO_NUM_13
#define DISPLAY_BLK    GPIO_NUM_14
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 240

// 全双工 i2s:扬声器与麦克风共享 bclk/ws,仅数据线独立
#define I2S_SPK_BCLK GPIO_NUM_47
#define I2S_SPK_LRCK GPIO_NUM_21
#define I2S_SPK_DOUT GPIO_NUM_35
#define I2S_MIC_SCK  GPIO_NUM_47
#define I2S_MIC_WS   GPIO_NUM_21
#define I2S_MIC_DIN  GPIO_NUM_2

// wifi 凭据由 menuconfig → "Rover-Car Configuration" 管理,见 Kconfig.projbuild