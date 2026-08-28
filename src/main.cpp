// ============================================================
//  SD2 小电视 DeepSeek 余额显示器
//
//  硬件: ESP8266(ESP-12F) + 1.3" ST7789 240x240 (无CS)
//  参考: https://github.com/Jason6111/sd2
//  接口: https://api-docs.deepseek.com/zh-cn/api/get-user-balance
//
//  公共基础功能来自 sd2-common（WiFi/NTP/休眠/背光/HTTP/格式化）
//  使用前先修改 src/config.h（WiFi / API Key）
//  界面直接用 TFT_eSPI 绘制（字体放 Flash，ESP8266 稳定）
// ============================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <SD2Common.h>
#include <Sd2App.h>

#include "config.h"
#include "DeepSeekClient.h"
#include "logo.h"

// ---------- 公共运行骨架（sd2-common）----------
static sd2::App app(POLL_INTERVAL_MS, SLEEP_START_HOUR, SLEEP_END_HOUR);
TFT_eSPI &tft = app.tft;
static sd2::Wifi &wifi = app.wifi;
static sd2::SleepScheduler &sleepSched = app.sleep;
static sd2::Backlight &backlight = app.backlight;
static bool &bootDone = app.bootDone;
static bool &ntpDone = app.ntpDone;
static uint32_t &lastFetchMs = app.lastFetchMs;

// ---------- 数据状态 ----------
static bool hasData = false;
static bool fetching = false;

static DeepSeekBalance lastData;
static time_t lastUpdateTime = 0; // 数据更新时间（本地时区）

// ---------- 界面工具 ----------
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
    String total = hasData ? sd2::trimNumber(lastData.total_balance) : "--";
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
        s = sd2::formatLocalTime(lastUpdateTime, "%Y-%m-%d %H:%M");
    }
    drawText((240 - textWidth(s, &FreeSans9pt7b)) / 2, 214, s, C_DATE, &FreeSans9pt7b);
}

void drawMainPage() {
    tft.startWrite();
    tft.fillScreen(C_BG);

    // 顶栏：左上角 DeepSeek 完整字标（鲸鱼 + deepseek）
    tft.pushImage(10, 1, DS_LOGO_W, DS_LOGO_H, ds_logo);
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

    drawRowValue(128, hasData ? sd2::trimNumber(lastData.topped_up_balance) : "--");
    drawRowValue(166, hasData ? sd2::trimNumber(lastData.granted_balance) : "--");
    drawDate();
    tft.endWrite();
}

// ---------- 公共骨架回调 ----------
static void onConnected() {
    if (!sleepSched.sleeping()) drawMainPage();
}

static void onWake() {
    if (bootDone) drawMainPage();
}

void handleFetch() {
    if (!wifi.connected() || fetching || sleepSched.sleeping()) return;
    if (millis() - lastFetchMs < POLL_INTERVAL_MS) return;

    // 证书校验前先等 NTP 时间同步
    if (!ntpDone) {
        if (sd2::timeSynced()) {
            ntpDone = true;
            Serial.printf("NTP time synced: %lu\n", (unsigned long)time(nullptr));
        }
        return;
    }
    if (sleepSched.sleeping()) return; // 休眠时段不拉取数据

    // 未填 Key 时直接提示
    if (strlen(DEEPSEEK_API_KEY) < 20 || strncmp(DEEPSEEK_API_KEY, "sk-", 3) != 0) {
        lastFetchMs = millis();
        return;
    }

    fetching = true;

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
        Serial.printf("Balance: %s %s (available=%d)\n",
                      d.total_balance.c_str(), d.currency.c_str(), d.is_available);
        tft.startWrite(); // 单事务批量重绘，避免逐块清空闪动
        drawBigTotal();
        drawRowValue(128, sd2::trimNumber(d.topped_up_balance));
        drawRowValue(166, sd2::trimNumber(d.granted_balance));
        drawDate();
        tft.endWrite();
    } else {
        Serial.printf("Fetch failed: %s (HTTP %d)\n", d.error.c_str(), d.http_code);
        if (!hasData) {
            tft.startWrite();
            drawBigTotal();
            tft.endWrite();
        }
    }
}

// ---------- 主程序 ----------
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println();
    Serial.println("SD2 DeepSeek Balance Monitor starting...");

    app.setHooks(drawBootPage, handleFetch, onConnected, nullptr, nullptr, onWake);
    app.begin(WIFI_SSID, WIFI_PASSWORD, BRIGHTNESS, TZ_OFFSET_SEC, NTP_SERVER);
}

void loop() {
    app.loop();
}
