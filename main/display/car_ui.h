#pragma once
#include <stdbool.h>
#include <stdint.h>

void car_ui_init(void);
void car_ui_update(int left_speed, int right_speed, bool ble_connected, bool wifi_connected);