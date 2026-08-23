#pragma once

// ============================================================
//  SD2 小电视 DeepSeek 余额显示器 - 用户配置模板
//  使用方法：复制本文件为 src/config.h 后填写你的配置
//    cp src/config.example.h src/config.h
//  config.h 已在 .gitignore 中，不会被提交到仓库
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

// ---- 定时休眠 ----
// 默认 00:00-07:00 关闭显示并停止获取数据（WiFi 保持连接，醒来立即恢复）
#define ENABLE_SLEEP 1
#define SLEEP_START_HOUR 0   // 开始休眠（本地时间，小时）
#define SLEEP_END_HOUR 7     // 结束休眠（本地时间，小时）

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
