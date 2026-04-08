#include "car_ui.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include "../config.h"

static const char* TAG = "car_ui";

// === 颜色定义（RGB565）===
#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_RED    0xF800
#define COLOR_GREEN  0x07E0
#define COLOR_BLUE   0x001F
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN   0x07FF
#define COLOR_GRAY   0x8410
#define COLOR_BG     0x1082  // 深灰背景

// === 显示配置 ===
#define LINE_BUF_LINES 16                             // 帧缓冲行数
#define FONT_SCALE     2                              // 字体放大倍数
#define CHAR_W         (5 * FONT_SCALE + FONT_SCALE)  // 字符宽度（含间距）
#define CHAR_H         (7 * FONT_SCALE + FONT_SCALE)  // 字符高度（含间距）

static esp_lcd_panel_handle_t    s_panel    = NULL;
static esp_lcd_panel_io_handle_t s_panel_io = NULL;

// 帧缓冲（用于逐行刷新）
static uint16_t s_line_buf[LINE_BUF_LINES][DISPLAY_WIDTH];

/*
 * 5x7 像素字体（ASCII 32-127）
 *
 * 使用 static const 存储，避免占用 RAM。
 * 每个字符 5 列，每列 7 位（从上到下）。
 */
static const uint8_t FONT5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00},  // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62},  // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50},  // '&'
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '\''
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14},  // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // '+'
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ','
    {0x08, 0x08, 0x08, 0x08, 0x08},  // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00},  // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02},  // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46},  // '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39},  // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03},  // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36},  // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00},  // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00},  // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14},  // '='
    {0x00, 0x41, 0x22, 0x14, 0x08},  // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06},  // '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // 'L'
    {0x7F, 0x02, 0x04, 0x02, 0x7F},  // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31},  // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63},  // 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07},  // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43},  // 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00},  // '['
    {0x02, 0x04, 0x08, 0x10, 0x20},  // '\\'
    {0x00, 0x41, 0x41, 0x7F, 0x00},  // ']'
    {0x04, 0x02, 0x01, 0x02, 0x04},  // '^'
    {0x40, 0x40, 0x40, 0x40, 0x40},  // '_'
    {0x00, 0x01, 0x02, 0x04, 0x00},  // '`'
    {0x20, 0x54, 0x54, 0x54, 0x78},  // 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38},  // 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20},  // 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F},  // 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18},  // 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02},  // 'f'
    {0x0C, 0x52, 0x52, 0x52, 0x3E},  // 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78},  // 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00},  // 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00},  // 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00},  // 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00},  // 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78},  // 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78},  // 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38},  // 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08},  // 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C},  // 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08},  // 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20},  // 's'
    {0x04, 0x3F, 0x44, 0x40, 0x20},  // 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C},  // 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C},  // 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C},  // 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44},  // 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C},  // 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44},  // 'z'
};

// === 初始化 ===
void car_ui_init(void) {
    // 背光控制
    gpio_config_t bl = {
        .pin_bit_mask = 1ULL << DISPLAY_BLK,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(DISPLAY_BLK, 1);

    // SPI 总线初始化
    spi_bus_config_t bus = {
        .mosi_io_num     = DISPLAY_MOSI,
        .miso_io_num     = GPIO_NUM_NC,
        .sclk_io_num     = DISPLAY_SCLK,
        .quadwp_io_num   = GPIO_NUM_NC,
        .quadhd_io_num   = GPIO_NUM_NC,
        .max_transfer_sz = DISPLAY_WIDTH * LINE_BUF_LINES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    // 面板 IO 配置（SPI）
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = DISPLAY_CS,
        .dc_gpio_num       = DISPLAY_DC,
        .spi_mode          = 3,
        .pclk_hz           = 80 * 1000 * 1000,  // 80MHz
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &s_panel_io));

    // ST7789 面板配置
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISPLAY_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_panel_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // 清屏
    for (int i = 0; i < LINE_BUF_LINES; i++) {
        for (int j = 0; j < DISPLAY_WIDTH; j++) {
            s_line_buf[i][j] = COLOR_BG;
        }
    }
    for (int y = 0; y < DISPLAY_HEIGHT; y += LINE_BUF_LINES) {
        int h = (y + LINE_BUF_LINES > DISPLAY_HEIGHT) ? DISPLAY_HEIGHT - y : LINE_BUF_LINES;
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISPLAY_WIDTH, y + h, s_line_buf);
    }

    ESP_LOGI(TAG, "init ok, %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

// === 绘图工具 ===

/**
 * 填充矩形区域
 * 通过分块传输减少内存占用，同时保持较好的传输效率
 */
static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0 || y < 0 || x + w > DISPLAY_WIDTH || y + h > DISPLAY_HEIGHT) return;

    for (int i = 0; i < LINE_BUF_LINES; i++) {
        for (int j = 0; j < w; j++) {
            s_line_buf[i][j] = (color << 8) | (color >> 8);  // 字节序转换 (SPI 传输需要)
        }
    }

    for (int row = 0; row < h; row += LINE_BUF_LINES) {
        int cur_h = (row + LINE_BUF_LINES > h) ? h - row : LINE_BUF_LINES;
        esp_lcd_panel_draw_bitmap(s_panel, x, y + row, x + w, y + row + cur_h, s_line_buf);
    }
}

/**
 * 绘制单个字符
 * 优化: 先在缓冲区准备好字符像素，一次性通过 SPI 发送，极大提升显示性能
 */
static void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg) {
    if (c < ' ' || c > 'z') c = ' ';
    const uint8_t* bmp = FONT5x7[c - ' '];

    // 字符缓冲区 (12x16 像素)
    static uint16_t char_buf[CHAR_H][CHAR_W];
    uint16_t        fg_le = (fg << 8) | (fg >> 8);
    uint16_t        bg_le = (bg << 8) | (bg >> 8);

    for (int i = 0; i < CHAR_H; i++) {
        for (int j = 0; j < CHAR_W; j++) {
            char_buf[i][j] = bg_le;
        }
    }

    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (bmp[col] >> row & 1) {
                // 缩放处理
                for (int sy = 0; sy < FONT_SCALE; sy++) {
                    for (int sx = 0; sx < FONT_SCALE; sx++) {
                        char_buf[row * FONT_SCALE + sy][col * FONT_SCALE + sx] = fg_le;
                    }
                }
            }
        }
    }

    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + CHAR_W, y + CHAR_H, char_buf);
}

static void draw_str(int x, int y, const char* s, uint16_t fg, uint16_t bg) {
    while (*s) {
        draw_char(x, y, *s++, fg, bg);
        x += CHAR_W;
    }
}

// === ui 刷新逻辑 ===
static int  s_last_left = 0, s_last_right = 0;
static bool s_last_ble = false, s_last_wifi = false;
static bool s_first = true;

void car_ui_update(int left_speed, int right_speed, bool ble_connected, bool wifi_connected) {
    // 状态无变化时跳过重绘，减少总线负载
    if (!s_first && left_speed == s_last_left && right_speed == s_last_right && ble_connected == s_last_ble &&
        wifi_connected == s_last_wifi) {
        return;
    }
    s_first      = false;
    s_last_left  = left_speed;
    s_last_right = right_speed;
    s_last_ble   = ble_connected;
    s_last_wifi  = wifi_connected;

    // 绘制标题栏 (蓝色背景)
    fill_rect(0, 0, DISPLAY_WIDTH, CHAR_H + 4, COLOR_BLUE);
    draw_str(4, 2, "ESP32-Rover-Car", COLOR_WHITE, COLOR_BLUE);

    // 绘制网络状态
    int sy = CHAR_H + 8;
    fill_rect(0, sy, DISPLAY_WIDTH, CHAR_H + 4, COLOR_BG);
    draw_str(4, sy, "BLE:", COLOR_GRAY, COLOR_BG);
    draw_str(4 + CHAR_W * 4, sy, ble_connected ? "ON " : "OFF", ble_connected ? COLOR_GREEN : COLOR_RED, COLOR_BG);
    draw_str(DISPLAY_WIDTH / 2, sy, "WiFi:", COLOR_GRAY, COLOR_BG);
    draw_str(DISPLAY_WIDTH / 2 + CHAR_W * 5, sy, wifi_connected ? "ON " : "OFF",
             wifi_connected ? COLOR_GREEN : COLOR_RED, COLOR_BG);

    // 绘制速度数值
    int bar_y = sy + CHAR_H + 16;
    fill_rect(0, bar_y, DISPLAY_WIDTH, (CHAR_H + 4) * 2 + 4, COLOR_BG);
    draw_str(4, bar_y, "L:", COLOR_GRAY, COLOR_BG);
    char buf[16];
    snprintf(buf, sizeof(buf), "%+4d", left_speed);
    draw_str(4 + CHAR_W * 2, bar_y, buf, COLOR_YELLOW, COLOR_BG);

    draw_str(4, bar_y + CHAR_H + 4, "R:", COLOR_GRAY, COLOR_BG);
    snprintf(buf, sizeof(buf), "%+4d", right_speed);
    draw_str(4 + CHAR_W * 2, bar_y + CHAR_H + 4, buf, COLOR_YELLOW, COLOR_BG);

    // 绘制方向指示器
    int dir_y = bar_y + (CHAR_H + 4) * 2 + 12;
    fill_rect(0, dir_y, DISPLAY_WIDTH, CHAR_H + 4, COLOR_BG);
    const char* dir_str   = "STOP";
    uint16_t    dir_color = COLOR_GRAY;

    if (left_speed > 0 && right_speed > 0) {
        dir_str   = "FORWARD";
        dir_color = COLOR_GREEN;
    } else if (left_speed < 0 && right_speed < 0) {
        dir_str   = "BACKWARD";
        dir_color = COLOR_RED;
    } else if (left_speed > 0 && right_speed <= 0) {
        dir_str   = "RIGHT";
        dir_color = COLOR_CYAN;
    } else if (left_speed <= 0 && right_speed > 0) {
        dir_str   = "LEFT";
        dir_color = COLOR_CYAN;
    }

    int dx = (DISPLAY_WIDTH - (int)strlen(dir_str) * CHAR_W) / 2;
    draw_str(dx, dir_y, dir_str, dir_color, COLOR_BG);
}