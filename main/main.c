#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "display/car_ui.h"
#include "led.h"
#include "motor.h"
#include "wifi_ap.h"

// 任务配置
#define UI_TASK_STACK       4096
#define UI_TASK_PRIORITY    3
#define UI_TASK_INTERVAL_MS 50

static const char* TAG = "main";

/**
 * ui 刷新任务
 * 定期从 motor 模块获取当前速度，并更新到显示屏
 */
static void task_ui(void* arg) {
    int left, right;
    while (1) {
        motor_get_speed(&left, &right);
        // 更新 ui: 左右速度、蓝牙状态(暂未实现)、wifi 状态(默认为开启)
        car_ui_update(left, right, false, true);
        vTaskDelay(pdMS_TO_TICKS(UI_TASK_INTERVAL_MS));
    }
}

/**
 * 启动灯光动画
 * 用于指示系统初始化过程
 */
static void boot_led_sequence(void) {
    led_set_all(LED_GREEN);
    led_flush();
    vTaskDelay(pdMS_TO_TICKS(300));

    led_set_all(LED_BLUE);
    led_flush();
    vTaskDelay(pdMS_TO_TICKS(300));

    led_off();
}

void app_main(void) {
    ESP_LOGI(TAG, "esp32-rover-car starting");

    // 初始化硬件模块
    motor_init();
    led_init();
    car_ui_init();
    wifi_ap_init();

    // 执行启动动画
    boot_led_sequence();

    // 创建后台 ui 刷新任务
    xTaskCreate(task_ui, "ui_task", UI_TASK_STACK, NULL, UI_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "system ready");
}