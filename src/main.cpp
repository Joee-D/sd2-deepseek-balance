// ============================================================
//  SD2 小电视 DeepSeek 余额显示器
//
//  硬件: ESP8266(ESP-12F) + 1.3" ST7789 240x240 (无CS) + WS2812
//  参考: https://github.com/Jason6111/sd2
//  接口: https://api-docs.deepseek.com/zh-cn/api/get-user-balance
//
//  使用前先修改 src/config.h（WiFi / API Key）
//  界面直接用 TFT_eSPI 绘制（字体放 Flash，ESP8266 稳定）
// ============================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "DeepSeekClient.h"
#include "logo.h"

#if USE_WS2812_STATUS
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel ws2812(1, 12, NEO_GRB + NEO_KHZ800);
#endif

TFT_eSPI tft = TFT_eSPI();

// ---------- 颜色 ----------
#define C_BG      tft.color565(0x0E, 0x11, 0x20)
#define C_CARD    tft.color565(0x1B, 0x22, 0x3C)
#define C_BORDER  tft.color565(0x2E, 0x38, 0x58)
#define C_LINE    tft.color565(0x23, 0x2B, 0x46)
#define C_SUB     tft.color565(0x8A, 0x94, 0xB8)
#define C_LABEL   tft.color565(0x9A, 0xA5, 0xC8)
#define C_DATE    tft.color565(0x8F, 0x9B, 0xBF)
#define C_SHADOW  tft.color565(0x0A, 0x0F, 0x2A)
#define C_ACCENT  tft.color565(0x4D, 0x6B, 0xFE)
#define C_WHITE   tft.color565(0xFF, 0xFF, 0xFF)
#define C_GREEN   tft.color565(0x34, 0xD3, 0x99)
#define C_RED     tft.color565(0xFF, 0x6B, 0x6B)
#define C_YELLOW  tft.color565(0xF6, 0xC3, 0x43)

// ---------- 状态 ----------
static bool wifiReady = false;
static bool bootDone = false;
static bool hasData = false;
static bool fetching = false;
static bool wifiFailShown = false;
static bool timeSynced = false;
static bool sleeping = false;
static uint32_t lastFetchMs = 0;
static uint32_t lastWifiAttempt = 0;
static uint32_t lastSleepCheck = 0;
static uint32_t lastHeapPrint = 0;
static uint32_t bootStart = 0;

static DeepSeekBalance lastData;
static uint16_t dotColor = C_YELLOW;
static time_t lastUpdateTime = 0; // 数据更新时间（本地时区）

// ---------- 工具 ----------
String prettyBalance(const String &s) {
    String r = s;
    int dot = r.indexOf('.');
    if (dot >= 0) {
        while (r.length() > 0 && r.endsWith("0")) r.remove(r.length() - 1);
        if (r.endsWith(".")) r.remove(r.length() - 1);
    }
    if (r.length() == 0) r = "0";
    return r;
}

void drawText(int x, int y, const String &s, uint16_t color, const GFXfont *font) {
    tft.setFreeFont(font);
    tft.setTextColor(color);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(s, x, y);
}

int textWidth(const String &s, const GFXfont *font) {
    tft.setFreeFont(font);
    return tft.textWidth(s);
}

void drawDot(int x, int y, uint16_t color) {
    tft.fillCircle(x, y, 5, color);
}

void setBacklight(int value) {
    pinMode(TFT_BL, OUTPUT);
#if BACKLIGHT_INVERTED
    analogWrite(TFT_BL, 1023 - value);
#else
    analogWrite(TFT_BL, value);
#endif
}

#if USE_WS2812_STATUS
void setLed(uint32_t rgb) {
    ws2812.setPixelColor(0, rgb);
    noInterrupts();
    ws2812.show();
    interrupts();
}
#endif

// ---------- 启动页 ----------
void drawBootPage(bool fail) {
    tft.fillScreen(C_BG);
    tft.pushImage((240 - DS_LOGO_W) / 2, 100, DS_LOGO_W, DS_LOGO_H, ds_logo);
    const char *hint = fail ? "WiFi failed, retrying..." : "Connecting WiFi...";
    int tw = textWidth(hint, &FreeSans9pt7b);
    drawText((240 - tw) / 2, 148, hint, fail ? C_RED : C_LABEL, &FreeSans9pt7b);
}

// ---------- 主页面元素 ----------
void drawBigTotal() {
    tft.fillRect(10, 44, 220, 64, C_BG); // 清除旧数字，避免残影
    String total = hasData ? prettyBalance(lastData.total_balance) : "--";
    tft.setTextFont(7); // 48px 七段数字（原版字体）
    tft.setTextDatum(TL_DATUM);
    int w = tft.textWidth(total, 7);
    int x = (240 - w) / 2;
    tft.setTextColor(C_SHADOW);            // 轻微投影
    tft.drawString(total, x + 1, 55);
    tft.setTextColor(C_WHITE);
    tft.drawString(total, x, 54);          // 居中显示
}

void drawRowValue(int y, const String &value) {
    tft.fillRect(124, y - 4, 102, 24, C_CARD); // 只清卡片内区域，用卡片底色，避免擦掉右边框
    drawText(128, y, value, C_WHITE, &FreeSans9pt7b);
}

void drawDate() {
    tft.fillRect(0, 206, 240, 34, C_BG);
    String s = "----";
    if (lastUpdateTime > 0) {
        struct tm tmv;
        localtime_r(&lastUpdateTime, &tmv);
        char buf[64];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min);
        s = buf;
    }
    drawText((240 - textWidth(s, &FreeSans9pt7b)) / 2, 214, s, C_DATE, &FreeSans9pt7b);
}

void drawMainPage() {
    tft.fillScreen(C_BG);

    // 顶栏：左上角 DeepSeek 完整字标（鲸鱼 + deepseek）
    tft.pushImage(10, 1, DS_LOGO_W, DS_LOGO_H, ds_logo);
    drawDot(224, 15, dotColor);
    tft.drawCircle(224, 15, 7, C_BORDER); // 状态点圆环
    tft.drawFastHLine(16, 40, 208, C_BORDER);

    // 总余额（大数字，无标题）
    drawBigTotal();

    // 卡片
    tft.fillRoundRect(12, 112, 216, 88, 12, C_CARD);
    tft.drawRoundRect(12, 112, 216, 88, 12, C_BORDER);
    tft.fillCircle(20, 134, 3, C_ACCENT);   // TOP-UP 圆点
    tft.fillCircle(20, 172, 3, C_GREEN);    // GRANTED 圆点
    drawText(26, 128, "TOP-UP", C_LABEL, &FreeSans9pt7b);
    drawText(26, 166, "GRANTED", C_LABEL, &FreeSans9pt7b);
    tft.drawFastHLine(36, 154, 168, C_BORDER);

    drawRowValue(128, hasData ? prettyBalance(lastData.topped_up_balance) : "--");
    drawRowValue(166, hasData ? prettyBalance(lastData.granted_balance) : "--");
    drawDate();
}

// ---------- 定时休眠 ----------
bool isSleepHour() {
    if (!timeSynced) return false; // 时间未同步时不休眠
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    int h = tmv.tm_hour;
#if SLEEP_START_HOUR < SLEEP_END_HOUR
    return (h >= SLEEP_START_HOUR && h < SLEEP_END_HOUR);
#else
    return (h >= SLEEP_START_HOUR || h < SLEEP_END_HOUR); // 跨天窗口
#endif
}

void updateSleep() {
#if ENABLE_SLEEP
    bool should = isSleepHour();
    if (should && !sleeping) {
        sleeping = true;
        analogWrite(TFT_BL, 1023); // 反相背光：高电平 = 关闭
#if USE_WS2812_STATUS
        setLed(ws2812.Color(0, 0, 0));
#endif
        Serial.println("Sleep mode: display off, fetch paused");
    } else if (!should && sleeping) {
        sleeping = false;
        setBacklight(BRIGHTNESS);
#if USE_WS2812_STATUS
        setLed(ws2812.Color(0, 80, 255));
#endif
        if (bootDone) drawMainPage();
        lastFetchMs = millis() - POLL_INTERVAL_MS; // 醒来立即刷新
        Serial.println("Wake up: display on");
    }
#endif
}

// ---------- 网络 ----------
void handleWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiReady) {
            wifiReady = true;
            Serial.print("WiFi connected, IP: ");
            Serial.println(WiFi.localIP().toString().c_str());
            bootDone = true;
            dotColor = C_YELLOW;
            if (!sleeping) drawMainPage();
            configTime(TZ_OFFSET_SEC, 0, NTP_SERVER, "pool.ntp.org");
            lastFetchMs = millis() - POLL_INTERVAL_MS; // 立即拉取
        }
        return;
    }

    if (wifiReady) {
        wifiReady = false;
        dotColor = C_YELLOW;
        if (bootDone) {
            drawDot(224, 15, dotColor);
        }
    }

    if (millis() - lastWifiAttempt > 5000) {
        lastWifiAttempt = millis();
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    if (!bootDone && !wifiFailShown && millis() - bootStart > 30000) {
        wifiFailShown = true;
        drawBootPage(true);
    }

#if USE_WS2812_STATUS
    setLed(ws2812.Color(0, 80, 255)); // 蓝 = 连接中
#endif
}

void handleFetch() {
    if (!wifiReady || fetching || sleeping) return;
    if (millis() - lastFetchMs < POLL_INTERVAL_MS) return;

    // 证书校验前先等 NTP 时间同步
    if (!timeSynced) {
        if (time(nullptr) > 1600000000) {
            timeSynced = true;
            Serial.printf("NTP time synced: %lu\n", (unsigned long)time(nullptr));
        }
        return;
    }
    if (isSleepHour()) return; // 休眠时段不拉取数据

    // 未填 Key 时直接提示
    if (strlen(DEEPSEEK_API_KEY) < 20 || strncmp(DEEPSEEK_API_KEY, "sk-", 3) != 0) {
        dotColor = C_RED;
        drawDot(224, 15, dotColor);
        lastFetchMs = millis();
        return;
    }

    fetching = true;
    dotColor = C_YELLOW;
    drawDot(224, 15, dotColor);

    DeepSeekBalance d;
    ESP.wdtDisable(); // TLS 握手为阻塞操作，暂时关闭软看门狗
    bool ok = fetchDeepSeekBalance(d);
    ESP.wdtEnable(0);
    fetching = false;
    lastFetchMs = millis();

    if (ok) {
        hasData = true;
        lastData = d;
        lastUpdateTime = time(nullptr);
        dotColor = C_GREEN;
        Serial.printf("Balance: %s %s (available=%d)\n",
                      d.total_balance.c_str(), d.currency.c_str(), d.is_available);
        drawBigTotal();
        drawRowValue(128, prettyBalance(d.topped_up_balance));
        drawRowValue(166, prettyBalance(d.granted_balance));
        drawDate();
    } else {
        dotColor = C_RED;
        Serial.printf("Fetch failed: %s (HTTP %d)\n", d.error.c_str(), d.http_code);
        if (!hasData) drawBigTotal();
    }
    drawDot(224, 15, dotColor);

#if USE_WS2812_STATUS
    setLed(ok ? ws2812.Color(0, 255, 60) : ws2812.Color(255, 40, 40));
#endif
    Serial.printf("Free heap: %u B\n", ESP.getFreeHeap());
}

// ---------- 主程序 ----------
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println();
    Serial.println("SD2 DeepSeek Balance Monitor starting...");

    tft.begin();
    tft.setRotation(0);
    setBacklight(BRIGHTNESS);

    bootStart = millis();
    drawBootPage(false);

#if USE_WS2812_STATUS
    ws2812.begin();
    ws2812.setBrightness(40);
    setLed(ws2812.Color(0, 80, 255));
#endif

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiAttempt = millis();
}

void loop() {
    handleWiFi();
    handleFetch();

    if (millis() - lastSleepCheck >= 1000) {
        lastSleepCheck = millis();
        updateSleep();
    }
    if (millis() - lastHeapPrint >= 10000) {
        lastHeapPrint = millis();
        Serial.printf("Free heap: %u B\n", ESP.getFreeHeap());
    }

    delay(20);
}
