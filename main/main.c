#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "display/car_ui.h"
#include "led.h"
#include "motor.h"
#include "net/captive_dns.h"
#include "net/http_server.h"
#include "net/wifi_ap.h"

#define UI_TASK_STACK       6144
#define UI_TASK_PRIORITY    3
#define UI_TASK_INTERVAL_MS 50
// 超时则判定遥控失联,强制停车
#define CMD_TIMEOUT_MS 500

static const char* TAG = "main";

// 同一个任务复用做指令看门狗,避免额外 tick 任务
static void task_ui(void* arg) {
    int left, right;
    while (1) {
        motor_watchdog_check(CMD_TIMEOUT_MS);
        motor_get_speed(&left, &right);
        car_ui_update(left, right, false, true);
        vTaskDelay(pdMS_TO_TICKS(UI_TASK_INTERVAL_MS));
    }
}

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
    ESP_LOGI(TAG, "booting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs partition corrupted, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    motor_init();
    led_init();
    car_ui_init();

    wifi_ap_start();
    captive_dns_start();
    http_server_start();

    boot_led_sequence();

    xTaskCreate(task_ui, "ui_task", UI_TASK_STACK, NULL, UI_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "system ready");
}
