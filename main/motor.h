#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { WHEEL_FL = 0, WHEEL_FR, WHEEL_RL, WHEEL_RR, WHEEL_COUNT } wheel_t;

void motor_init(void);

// speed 范围 [-255, 255],正值前进,负值后退
void motor_set_wheel(wheel_t wheel, int speed);
void motor_drive(int left, int right);
void motor_stop(void);
void motor_get_speed(int* left, int* right);

// 距离上次 motor_drive 超过 timeout_ms 则强制停车,返回 true 表示本次触发
bool motor_watchdog_check(uint32_t timeout_ms);