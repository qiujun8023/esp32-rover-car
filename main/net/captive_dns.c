#include "net/captive_dns.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char* TAG = "captive_dns";

// DNS 报文头字段偏移，见 RFC 1035 4.1.1
#define DNS_HDR_LEN    12
#define DNS_FLAGS_HI   2
#define DNS_FLAGS_LO   3
#define DNS_ANCOUNT_HI 6
#define DNS_ANCOUNT_LO 7

// 响应标志：QR=1 / AA=1 / RD=1 / RA=1
#define DNS_FLAGS_HI_RESP 0x81
#define DNS_FLAGS_LO_RESP 0x80
#define DNS_ANSWER_COUNT  0x01

// 追加应答固定占 16 字节：2 ptr + 2 type + 2 class + 4 ttl + 2 rdlen + 4 ip
#define DNS_ANSWER_LEN 16

static const uint8_t  AP_IP[4]  = {192, 168, 4, 1};
static const uint32_t A_TTL_SEC = 60;

// WiFi AP 就绪前 bind 53 会拿到 EADDRNOTAVAIL，必须退避重试，否则静默挂掉
static int open_and_bind(void) {
    for (int attempt = 0; attempt < 10; attempt++) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            ESP_LOGW(TAG, "socket fail, retry %d", attempt);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        struct sockaddr_in addr = {
            .sin_family      = AF_INET,
            .sin_port        = htons(53),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0)
            return sock;

        ESP_LOGW(TAG, "bind 53 fail, retry %d", attempt);
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGE(TAG, "bind 53 gave up");
    return -1;
}

static void dns_task(void* arg) {
    int sock = open_and_bind();
    if (sock < 0) {
        vTaskDelete(NULL);
        return;
    }

    // recv 超时用于周期性脱离 block，避免未来加停止信号时卡死
    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 请求缓冲原地改写并追加 answer 回发，省掉一份 512 字节栈副本
    uint8_t            buf[512];
    struct sockaddr_in cli;

    while (1) {
        socklen_t clen = sizeof(cli);
        int       n    = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&cli, &clen);
        if (n < 0)
            continue;
        // 太短非法 / 太长会让追加应答溢出 buf
        if (n < DNS_HDR_LEN || n > (int)sizeof(buf) - DNS_ANSWER_LEN)
            continue;

        uint8_t* resp = buf;
        resp[DNS_FLAGS_HI]   = DNS_FLAGS_HI_RESP;
        resp[DNS_FLAGS_LO]   = DNS_FLAGS_LO_RESP;
        resp[DNS_ANCOUNT_HI] = 0x00;
        resp[DNS_ANCOUNT_LO] = DNS_ANSWER_COUNT;

        int p = n;
        // NAME 使用指针压缩指向 header 后第一个问题名
        resp[p++] = 0xC0;
        resp[p++] = 0x0C;
        // TYPE=A
        resp[p++] = 0x00;
        resp[p++] = 0x01;
        // CLASS=IN
        resp[p++] = 0x00;
        resp[p++] = 0x01;
        // TTL，网络字节序
        resp[p++] = (A_TTL_SEC >> 24) & 0xFF;
        resp[p++] = (A_TTL_SEC >> 16) & 0xFF;
        resp[p++] = (A_TTL_SEC >> 8) & 0xFF;
        resp[p++] = A_TTL_SEC & 0xFF;
        // RDLENGTH=4
        resp[p++] = 0x00;
        resp[p++] = 0x04;
        resp[p++] = AP_IP[0];
        resp[p++] = AP_IP[1];
        resp[p++] = AP_IP[2];
        resp[p++] = AP_IP[3];

        sendto(sock, resp, p, 0, (struct sockaddr*)&cli, clen);
    }
}

void captive_dns_start(void) {
    xTaskCreate(dns_task, "captive_dns", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "captive dns ready");
}
