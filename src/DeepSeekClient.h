#pragma once

// DeepSeek 余额查询客户端
// 接口文档: https://api-docs.deepseek.com/zh-cn/api/get-user-balance
// GET https://api.deepseek.com/user/balance
// Authorization: Bearer <API Key>
// 底层 HTTP 读取/解析由 sd2-common 的 sd2::readHttpResponse 提供。

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD2Common.h>

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

static sd2::Https https(DIGICERT_GLOBAL_ROOT_G2_PEM, VERIFY_TLS_CERT);

static bool fetchDeepSeekBalance(DeepSeekBalance &out, uint32_t timeout_ms = 8000) {
    out = DeepSeekBalance();

    sd2::HttpResponse resp;
    String error;
    if (!https.get("api.deepseek.com", "/user/balance",
                   DEEPSEEK_API_KEY, "SD2-DeepSeek-Balance/1.0",
                   resp, error, timeout_ms)) {
        out.error = error;
        return false;
    }
    out.http_code = resp.httpCode;
    Serial.printf("HTTP %d\n", out.http_code);

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, resp.body);
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
