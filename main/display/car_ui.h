#pragma once
#include <stdbool.h>
#include <stdint.h>

// 初始化显示屏
void car_ui_init(void);

// 刷新显示内容
// 参数：左右轮速度、BLE 连接状态、WiFi 连接状态
void car_ui_update(int left_speed, int right_speed, bool ble_connected, bool wifi_connected);