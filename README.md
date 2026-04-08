# esp32-rover-car

基于 ESP32-S3 的四轮差速小车。ST7789 彩屏实时显示速度与状态，手机连接 WiFi 热点后通过虚拟摇杆实时控制移动，内置 DNS 劫持实现强制门户自动弹出控制页。

---

## 硬件

| 配件 | 规格 |
|------|------|
| 主控 | ESP32-S3 |
| 屏幕 | 1.3" IPS ST7789，240×240，SPI |
| 电机 | N20 减速电机 × 4 |
| 驱动 | TB6612 电机驱动模块 × 2 |
| 灯效 | WS2812 RGB 灯带 × 8 |

## 引脚

| GPIO | 功能 |
|------|------|
| 9 / 10 | ST7789 SCLK / MOSI |
| 11 / 12 | ST7789 CS / DC |
| 13 / 14 | ST7789 RST / BLK |
| 4 / 5 / 6 | 后左 PWM / IN2 / IN1 |
| 15 / 16 / 17 | 前左 IN1 / IN2 / PWM |
| 7 / 39 | 左板 / 右板 STBY |
| 36 / 37 / 38 | 前右 PWM / IN2 / IN1 |
| 40 / 41 / 42 | 后右 IN1 / IN2 / PWM |
| 18 | WS2812 LED |

---

## 编译烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## 使用

上电后手机连接 WiFi **`esp32-rover-car`**（无密码），浏览器访问 `10.10.10.10` 打开控制页面。
