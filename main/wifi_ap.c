#include "wifi_ap.h"

#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <math.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "motor.h"

// 配置常量
#define WS_BUF_SIZE       128    // WebSocket 接收缓冲区大小
#define JOYSTICK_DEADZONE 0.14f  // 摇杆死区半径（归一化坐标）
#define DNS_TASK_STACK    3072   // DNS 任务栈大小
#define AP_CHANNEL        1      // WiFi AP 信道
#define AP_MAX_CONN       4      // 最大连接数
#define AP_IP_ADDR        "10.10.10.10"
#define AP_NETMASK        "255.255.255.0"
#define HTTP_KEEPALIVE_MS 3000  // WebSocket 心跳间隔

static const char*    TAG      = "wifi_ap";
static httpd_handle_t s_server = NULL;

// http 控制页面 (单文件实现)
static const char s_html[] =
    "<!DOCTYPE html><html lang='zh-CN'>"
    "<head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>"
    "<title>ESP32-Rover-Car</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#0a0e1a;color:#00ffe1;font-family:Arial,sans-serif;"
    "display:flex;flex-direction:column;align-items:center;justify-content:center;"
    "min-height:100vh;gap:24px;padding:16px}"
    "h2{font-size:1.2rem;letter-spacing:3px;text-shadow:0 0 12px #00ffe1}"
    "#joystick-area{position:relative;width:200px;height:200px;"
    "border-radius:50%;border:2px solid rgba(0,255,225,0.4);"
    "background:rgba(0,255,225,0.05);touch-action:none}"
    "#stick{position:absolute;width:64px;height:64px;border-radius:50%;"
    "background:radial-gradient(circle,#00ffe1,#00b3a4);"
    "box-shadow:0 0 16px #00ffe1;top:68px;left:68px;pointer-events:none}"
    "#status{font-size:.75rem;opacity:.5;letter-spacing:1px}"
    "#speed{font-size:.85rem;opacity:.7;letter-spacing:1px}"
    "</style></head><body>"
    "<h2>ESP32-Rover-Car</h2>"
    "<div id='joystick-area'><div id='stick'></div></div>"
    "<div id='speed'>L: 0 &nbsp; R: 0</div>"
    "<div id='status'>connecting...</div>"
    "<script>"
    "var ws,joy=document.getElementById('joystick-area'),"
    "stick=document.getElementById('stick'),"
    "R=100,r=32,dragging=false,ox=0,oy=0;"
    "function connect(){"
    "  ws=new WebSocket('ws://'+location.host+'/ws');"
    "  ws.onopen=function(){document.getElementById('status').textContent='connected';};"
    "  "
    "ws.onclose=function(){document.getElementById('status').textContent='reconnecting...';setTimeout(connect,2000);};"
    "  ws.onerror=function(){ws.close();};"
    "}"
    "connect();"
    "setInterval(function(){if(ws&&ws.readyState===1)ws.send('{} ');},3000);"
    "document.addEventListener('visibilitychange',function(){"
    "  if(!document.hidden&&(!ws||ws.readyState!==1))connect();"
    "});"
    "function send(obj){if(ws&&ws.readyState===1)ws.send(JSON.stringify(obj));}"
    "function moveTo(x,y){"
    "  var d=Math.sqrt(x*x+y*y);"
    "  if(d>R-r){x=x/d*(R-r);y=y/d*(R-r);}"
    "  stick.style.left=(R-r+x)+'px';stick.style.top=(R-r+y)+'px';"
    "  var jx=x/(R-r),jy=y/(R-r);"
    "  var fl=-jy+jx,fr=-jy-jx;"
    "  var mv=Math.max(Math.abs(fl),Math.abs(fr));"
    "  if(mv>1){fl/=mv;fr/=mv;}"
    "  document.getElementById('speed').textContent="
    "    'L: '+Math.round(fl*255)+' \\u00a0 R: '+Math.round(fr*255);"
    "  send({j:[jx,jy]});"
    "}"
    "function resetStick(){"
    "  stick.style.left=(R-r)+'px';stick.style.top=(R-r)+'px';"
    "  document.getElementById('speed').textContent='L: 0 \\u00a0 R: 0';"
    "  send({j:[0,0]});"
    "}"
    "joy.addEventListener('touchstart',function(e){"
    "  e.preventDefault();dragging=true;"
    "  var rect=joy.getBoundingClientRect();ox=rect.left+R;oy=rect.top+R;"
    "},{passive:false});"
    "joy.addEventListener('touchmove',function(e){"
    "  e.preventDefault();if(!dragging)return;"
    "  var t=e.touches[0];moveTo(t.clientX-ox,t.clientY-oy);"
    "},{passive:false});"
    "joy.addEventListener('touchend',function(e){"
    "  e.preventDefault();dragging=false;resetStick();"
    "},{passive:false});"
    "joy.addEventListener('mousedown',function(e){"
    "  dragging=true;var rect=joy.getBoundingClientRect();"
    "  ox=rect.left+R;oy=rect.top+R;moveTo(e.clientX-ox,e.clientY-oy);"
    "});"
    "window.addEventListener('mousemove',function(e){if(dragging)moveTo(e.clientX-ox,e.clientY-oy);});"
    "window.addEventListener('mouseup',function(){if(dragging){dragging=false;resetStick();}});"
    "</script></body></html>";

/**
 * 差速驱动逻辑解析:
 * 摇杆坐标 (jx, jy) 范围 [-1, 1]
 * jy < 0 前进, jy > 0 后退
 * jx < 0 左转, jx > 0 右转
 */
static void parse_cmd(const char* data, size_t len) {
    const char* j_pos = strstr(data, "\"j\"");
    if (!j_pos) return;

    float       jx = 0, jy = 0;
    const char* bracket = strstr(j_pos, ":[");
    if (!bracket || sscanf(bracket + 2, "%f,%f", &jx, &jy) != 2) {
        return;
    }

    // 死区检测
    if (jx * jx + jy * jy < JOYSTICK_DEADZONE * JOYSTICK_DEADZONE) {
        motor_stop();
        return;
    }

    // 差速计算
    float fl   = -jy + jx;
    float fr   = -jy - jx;
    float maxv = (float)fabs(fl);
    if ((float)fabs(fr) > maxv) maxv = (float)fabs(fr);
    if (maxv > 1.0f) {
        fl /= maxv;
        fr /= maxv;
    }

    motor_drive((int)(fl * 255), (int)(fr * 255));
}

static esp_err_t handle_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) return ESP_OK;

    httpd_ws_frame_t pkt;
    uint8_t          buf[WS_BUF_SIZE] = {0};
    memset(&pkt, 0, sizeof(pkt));
    pkt.payload = buf;
    pkt.type    = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) return ret;

    // 防止缓冲区越界
    if (pkt.len >= WS_BUF_SIZE) {
        ESP_LOGW(TAG, "ws packet too large: %d", pkt.len);
        return ESP_OK;
    }
    buf[pkt.len] = '\0';
    parse_cmd((char*)buf, pkt.len);
    return ESP_OK;
}

/**
 * dns 劫持实现:
 * 监听 udp 53 端口，将所有域名解析到本地 ip，实现强制门户。
 */
static void dns_task(void* arg) {
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "dns server started on port 53");

    uint8_t            buf[512];
    struct sockaddr_in client;
    socklen_t          clen = sizeof(client);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&client, &clen);
        if (n < 12) continue;

        // 构造 dns 响应 (解析到 10.10.10.10)
        // 追加 16 字节 answer section，检查空间是否足够
        if (n + 16 > (int)sizeof(buf)) {
            ESP_LOGW(TAG, "dns query too large: %d, skipping", n);
            continue;
        }

        uint8_t resp[512];
        memset(resp, 0, sizeof(resp));
        memcpy(resp, buf, n);
        resp[2] = 0x81;
        resp[3] = 0x80;  // flags: qr=1, rd=1, ra=1, rcode=0
        resp[7] = 0x01;  // ancount=1

        int pos     = n;
        resp[pos++] = 0xc0;
        resp[pos++] = 0x0c;  // 指向查询域名
        resp[pos++] = 0x00;
        resp[pos++] = 0x01;  // type a
        resp[pos++] = 0x00;
        resp[pos++] = 0x01;  // class in
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 60;  // ttl
        resp[pos++] = 0x00;
        resp[pos++] = 0x04;  // rdlength=4
        resp[pos++] = 10;
        resp[pos++] = 10;
        resp[pos++] = 10;
        resp[pos++] = 10;  // ip: 10.10.10.10

        sendto(sock, resp, pos, 0, (struct sockaddr*)&client, clen);
    }
}

static void ap_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* e = (wifi_event_ap_staconnected_t*)data;
        ESP_LOGI(TAG, "client connected: " MACSTR, MAC2STR(e->mac));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* e = (wifi_event_ap_stadisconnected_t*)data;
        ESP_LOGI(TAG, "client disconnected: " MACSTR, MAC2STR(e->mac));
        motor_stop();  // 掉线自动停机，安全第一
    }
}

void wifi_ap_init(void) {
    // 初始化 nvs (wifi 栈需要)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ap_event_handler, NULL, NULL));

    wifi_config_t ap_cfg = {
        .ap =
            {
                .ssid_len       = sizeof(WIFI_AP_SSID) - 1,
                .channel        = AP_CHANNEL,
                .authmode       = WIFI_AUTH_OPEN,
                .max_connection = AP_MAX_CONN,
            },
    };
    memcpy(ap_cfg.ap.ssid, WIFI_AP_SSID, sizeof(WIFI_AP_SSID));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 配置静态 ip 地址
    esp_netif_ip_info_t ip_info = {
        .ip      = {.addr = ESP_IP4TOADDR(10, 10, 10, 10)},
        .gw      = {.addr = ESP_IP4TOADDR(10, 10, 10, 10)},
        .netmask = {.addr = ESP_IP4TOADDR(255, 255, 255, 0)},
    };
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    ESP_LOGI(TAG, "ap started: ssid=%s, ip=" AP_IP_ADDR, WIFI_AP_SSID);

    // 启动 http 服务器
    httpd_config_t hcfg   = HTTPD_DEFAULT_CONFIG();
    hcfg.lru_purge_enable = true;
    hcfg.max_uri_handlers = 8;
    hcfg.uri_match_fn     = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&s_server, &hcfg));

    static const httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = handle_root};
    static const httpd_uri_t uri_ws   = {.uri = "/ws", .method = HTTP_GET, .handler = handle_ws, .is_websocket = true};
    static const httpd_uri_t uri_any  = {.uri = "/*", .method = HTTP_GET, .handler = handle_root};

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_ws);
    httpd_register_uri_handler(s_server, &uri_any);

    // 启动 dns 劫持任务
    xTaskCreate(dns_task, "dns", DNS_TASK_STACK, NULL, 4, NULL);

    ESP_LOGI(TAG, "http server ready");
}