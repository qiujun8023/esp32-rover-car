#pragma once
#include <stdint.h>

// 车轮编号
typedef enum {
    WHEEL_FL = 0,  // 前左
    WHEEL_FR,      // 前右
    WHEEL_RL,      // 后左
    WHEEL_RR,      // 后右
    WHEEL_COUNT
} wheel_t;

void motor_init(void);

// 单轮速度控制，speed 范围 [-255, 255]
// 正值前进，负值后退，零值停止
void motor_set_wheel(wheel_t wheel, int speed);

// 整车差速控制，left/right 范围 [-255, 255]
void motor_drive(int left, int right);

void motor_stop(void);

// 获取当前速度
void motor_get_speed(int* left, int* right);