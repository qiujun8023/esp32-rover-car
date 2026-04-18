#include "net/http_server.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "motor.h"

static const char* TAG = "http";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

// captive portal 客户端白名单,循环覆盖
static uint32_t          s_portal_done[CONFIG_ESP_MAX_STA_CONN];
static uint8_t           s_portal_count = 0;
static uint8_t           s_portal_head  = 0;
static SemaphoreHandle_t s_portal_lock  = NULL;

static uint32_t get_client_ip(httpd_req_t* req) {
    int                fd = httpd_req_to_sockfd(req);
    struct sockaddr_in addr;
    socklen_t          len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr*)&addr, &len) != 0)
        return 0;
    return addr.sin_addr.s_addr;
}

static bool portal_is_done_locked(uint32_t ip) {
    for (int i = 0; i < s_portal_count; i++) {
        if (s_portal_done[i] == ip)
            return true;
    }
    return false;
}

static bool portal_is_done(uint32_t ip) {
    if (!s_portal_lock)
        return false;
    xSemaphoreTake(s_portal_lock, portMAX_DELAY);
    bool r = portal_is_done_locked(ip);
    xSemaphoreGive(s_portal_lock);
    return r;
}

// 满后按 fifo 覆盖最老条目,避免长时间运行后拒绝新客户端
static void portal_mark_done(uint32_t ip) {
    if (!ip || !s_portal_lock)
        return;
    xSemaphoreTake(s_portal_lock, portMAX_DELAY);
    if (!portal_is_done_locked(ip)) {
        s_portal_done[s_portal_head] = ip;
        s_portal_head                = (s_portal_head + 1) % CONFIG_ESP_MAX_STA_CONN;
        if (s_portal_count < CONFIG_ESP_MAX_STA_CONN)
            s_portal_count++;
    }
    xSemaphoreGive(s_portal_lock);
}

// 协议 {"j":[x,y]};差速驱动:jy<0 前进,归一化后映射到 [-255,255]
static void parse_cmd(const char* data) {
    cJSON* root = cJSON_Parse(data);
    if (!root)
        return;

    cJSON* j = cJSON_GetObjectItem(root, "j");
    if (!cJSON_IsArray(j) || cJSON_GetArraySize(j) < 2) {
        cJSON_Delete(root);
        return;
    }

    cJSON* jx_item = cJSON_GetArrayItem(j, 0);
    cJSON* jy_item = cJSON_GetArrayItem(j, 1);
    // 非数字的 jx/jy 会让下面 (int)(NaN*255) 触发 UB,直接丢帧
    if (!cJSON_IsNumber(jx_item) || !cJSON_IsNumber(jy_item)) {
        cJSON_Delete(root);
        return;
    }
    float jx = (float)cJSON_GetNumberValue(jx_item);
    float jy = (float)cJSON_GetNumberValue(jy_item);
    cJSON_Delete(root);

    // 摇杆死区,避免悬空抖动驱动电机
    if (jx * jx + jy * jy < 0.02f) {
        motor_stop();
        return;
    }

    float fl   = -jy + jx;
    float fr   = -jy - jx;
    float maxv = fabsf(fl);
    if (fabsf(fr) > maxv)
        maxv = fabsf(fr);
    if (maxv > 1.0f) {
        fl /= maxv;
        fr /= maxv;
    }

    motor_drive((int)(fl * 255), (int)(fr * 255));
}

static esp_err_t handle_index(httpd_req_t* req) {
    portal_mark_done(get_client_ip(req));
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char*)index_html_start, index_html_end - index_html_start);
}

static esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        ESP_LOGD(TAG, "ws connected, fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // 先探长度,过滤非文本 / 超长 / 空帧,避免拷贝溢出或无效读取
    httpd_ws_frame_t pkt = {.type = HTTPD_WS_TYPE_TEXT};
    esp_err_t        ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK)
        return ret;
    if (pkt.type != HTTPD_WS_TYPE_TEXT || pkt.len == 0 || pkt.len > 127)
        return ESP_OK;

    uint8_t buf[128] = {0};
    pkt.payload      = buf;
    ret              = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (ret != ESP_OK)
        return ret;

    buf[pkt.len] = '\0';
    parse_cmd((char*)buf);
    return ESP_OK;
}

// 已通过 portal 的客户端,对各平台连通性探测返回预期内容,避免反复弹窗 / 切 4g
static esp_err_t reply_connectivity_success(httpd_req_t* req) {
    const char* uri = req->uri;

    // Android / Chrome
    if (strstr(uri, "generate_204") || strstr(uri, "gen_204")) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }
    // Apple iOS / macOS
    if (strstr(uri, "hotspot-detect.html")) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_sendstr(req, "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    }
    // Windows
    if (strstr(uri, "connecttest.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Microsoft Connect Test");
    }
    if (strstr(uri, "ncsi.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Microsoft NCSI");
    }
    // Firefox
    if (strstr(uri, "canonical.html") || strstr(uri, "success.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "success\n");
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_sendstr(req, "Redirect to captive portal");
}

// 新客户端 303 触发 portal 弹窗;已通过的走 reply_connectivity_success
static esp_err_t handle_404(httpd_req_t* req, httpd_err_code_t err) {
    (void)err;
    uint32_t ip = get_client_ip(req);
    if (ip && portal_is_done(ip)) {
        return reply_connectivity_success(req);
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    // ios 需要非空 body 才会判定为 captive 并弹窗
    return httpd_resp_sendstr(req, "Redirect to captive portal");
}

void http_server_start(void) {
    if (!s_portal_lock)
        s_portal_lock = xSemaphoreCreateMutex();

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;
    // 防止假死客户端长期占用 socket
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;

    // captive portal 会产生大量重定向,默认日志会刷屏,降噪
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    httpd_handle_t srv = NULL;
    ESP_ERROR_CHECK(httpd_start(&srv, &cfg));

    httpd_uri_t u_index = {.uri = "/", .method = HTTP_GET, .handler = handle_index};
    httpd_uri_t u_ws    = {.uri = "/ws", .method = HTTP_GET, .handler = handle_ws, .is_websocket = true};

    httpd_register_uri_handler(srv, &u_index);
    httpd_register_uri_handler(srv, &u_ws);
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, handle_404);

    ESP_LOGI(TAG, "http server ready");
}
