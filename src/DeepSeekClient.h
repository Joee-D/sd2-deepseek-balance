#pragma once

// DeepSeek 余额查询客户端
// 接口文档: https://api-docs.deepseek.com/zh-cn/api/get-user-balance
// GET https://api.deepseek.com/user/balance
// Authorization: Bearer <API Key>

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "config.h"
#include "cert.h"

struct DeepSeekBalance {
    bool ok = false;            // 请求 + 解析是否成功
    bool is_available = false;  // 余额是否足够调用 API
    int http_code = 0;
    String currency;            // CNY / USD
    String total_balance;       // 总余额（含赠送）
    String granted_balance;     // 赠送余额
    String topped_up_balance;   // 充值余额
    String error;               // 中文错误描述
};

static BearSSL::X509List dsTrustAnchor(DIGICERT_GLOBAL_ROOT_G2_PEM);

static bool fetchDeepSeekBalance(DeepSeekBalance &out, uint32_t timeout_ms = 8000) {
    out = DeepSeekBalance();

    WiFiClientSecure client;
#if VERIFY_TLS_CERT
    client.setTrustAnchors(&dsTrustAnchor);
#else
    client.setInsecure();
#endif
    client.setTimeout(timeout_ms / 1000);

    uint32_t t0 = millis();
    if (!client.connect("api.deepseek.com", 443)) {
        char sslErr[64] = {0};
        client.getLastSSLError(sslErr, sizeof(sslErr));
        Serial.printf("TLS connect failed after %lu ms, ssl=%s\n", millis() - t0, sslErr);
        out.error = "Network error";
        return false;
    }

    client.print("GET /user/balance HTTP/1.1\r\n"
                 "Host: api.deepseek.com\r\n"
                 "Authorization: Bearer ");
    client.print(DEEPSEEK_API_KEY);
    client.print("\r\n"
                 "User-Agent: SD2-DeepSeek-Balance/1.0\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n\r\n");
    client.flush();

    // 手动读取原始响应（最多 6 秒）
    String raw;
    uint32_t r0 = millis();
    while (millis() - r0 < 6000) {
        while (client.available()) {
            raw += (char)client.read();
        }
        if (raw.length() > 0 && !client.connected() && client.available() == 0) break;
        delay(10);
    }

    // 拆分状态行与响应体
    String statusLine, body;
    int hEnd = raw.indexOf("\r\n\r\n");
    if (hEnd < 0) hEnd = raw.indexOf("\n\n");
    if (hEnd >= 0) {
        statusLine = raw.substring(0, hEnd);
        body = raw.substring(hEnd + (raw.indexOf("\r\n\r\n") >= 0 ? 4 : 2));
    } else {
        statusLine = raw;
    }
    out.http_code = 0;
    int sp2 = statusLine.indexOf(' ');
    int sp3 = statusLine.indexOf(' ', sp2 + 1);
    if (sp2 > 0 && sp3 > sp2) {
        out.http_code = statusLine.substring(sp2 + 1, sp3).toInt();
    }
    Serial.printf("HTTP %d (%lu ms)\n", out.http_code, millis() - t0);

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        out.error = "Response error";
        return false;
    }

    if (out.http_code == 200) {
        out.is_available = doc["is_available"] | false;
        JsonObject info = doc["balance_infos"][0];
        out.currency = info["currency"] | "CNY";
        out.total_balance = info["total_balance"].as<String>();
        out.granted_balance = info["granted_balance"].as<String>();
        out.topped_up_balance = info["topped_up_balance"].as<String>();
        if (out.total_balance.length() == 0) {
            out.error = "Response error";
            return false;
        }
        out.ok = true;
        return true;
    }

    // 常见错误码（返回体是 {"error":{"message":...,"code":"..."}}）
    if (out.http_code == 401 || out.http_code == 403) {
        out.error = "Bad API key";
    } else if (out.http_code == 429) {
        out.error = "HTTP 429";
    } else if (out.http_code >= 500) {
        out.error = "Server error";
    } else {
        out.error = "HTTP " + String(out.http_code);
    }
    return false;
}
