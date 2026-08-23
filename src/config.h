#pragma once

// ============================================================
//  SD2 小电视 DeepSeek 余额显示器 - 用户配置
//  修改后保存，然后在 VS Code 中点击 PlatformIO: Upload
// ============================================================

// ---- WiFi ----
const char WIFI_SSID[] = "your-wifi-ssid";
const char WIFI_PASSWORD[] = "your-wifi-password";

// ---- DeepSeek API Key ----
// 在 https://platform.deepseek.com/api_keys 创建，以 sk- 开头
const char DEEPSEEK_API_KEY[] = "sk-your-deepseek-api-key";

// ---- 刷新间隔（毫秒）----
const uint32_t POLL_INTERVAL_MS = 60UL * 1000UL; // 默认 60 秒

// ---- HTTPS 证书校验 ----
// 1 = 校验证书（默认，内置 DigiCert Global Root G2，见 cert.h）
// 0 = 跳过证书校验（部分网络环境证书链异常时应急用，不推荐）
#define VERIFY_TLS_CERT 1

// ---- NTP 时间同步 ----
// ESP8266 无 RTC，校验证书前必须先同步系统时间（时区按东八区）
#define NTP_SERVER "ntp.aliyun.com"
#define TZ_OFFSET_SEC (8UL * 3600UL)

// ---- 板载 WS2812 状态灯（GPIO12）----
// 连接中=蓝 成功=绿 失败=红；不需要可设为 0
#define USE_WS2812_STATUS 1

// ---- 屏幕背光 ----
// SD2 背光接 GPIO5(D1)，ESP8266 analogWrite 范围 0~1023
// SD2 背光为反相 PWM（值越大越暗），代码里会自动反转
#define TFT_BL 5
#define BACKLIGHT_INVERTED 1   // 1=反相（SD2 标准），0=正相
#define BRIGHTNESS 800         // 0~1023，数值越大越亮

// ---- 串口 ----
#define SERIAL_BAUD 921600
