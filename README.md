# SD2 小电视 · DeepSeek 余额显示器

在 [SD2 小电视](https://oshwhub.com/Q21182889/esp-xiao-dian-shi)（ESP8266 + 1.3 寸 ST7789 240×240 屏幕）上直接显示 DeepSeek API 余额的小固件。

固件在设备上通过 HTTPS 直接请求 DeepSeek 官方接口 [`GET https://api.deepseek.com/user/balance`](https://api-docs.deepseek.com/zh-cn/api/get-user-balance)，不需要任何额外客户端/服务器。

## 屏幕效果

![屏幕显示效果](images/screenshot.jpg)

## 显示内容（英文界面）

- 总余额（48px 七段大数字，居中显示，不显示币种）
- **TOP-UP**：充值余额；**GRANTED**：赠送余额
- 底部显示数据日期时间（如 2026-08-23 11:39）
- 定时休眠：默认 00:00-07:00 关闭显示并停止获取数据（`config.h` 可改）
- 默认每 60 秒自动刷新（`config.h` 可改）

界面直接用 TFT_eSPI 绘制（内置字体放 Flash，ESP8266 上稳定可靠，不引入 LVGL）。

## 硬件

| 项目 | 说明 |
|------|------|
| 主控 | ESP8266（ESP-12E/F），PlatformIO `board = nodemcuv2` |
| 屏幕 | ST7789，240×240，SPI，**无 CS**，MISO 未接 |
| 烧录 | Micro USB + 板载 CH340 |

引脚（已写入 `platformio.ini` 的 TFT_eSPI 宏）：

| 功能 | NodeMCU | GPIO |
|------|---------|------|
| TFT DC | D3 | GPIO0 |
| TFT RST | D4 | GPIO2 |
| TFT SCLK | D5 | GPIO14 |
| TFT MOSI | D7 | GPIO13 |
| 背光 | D1 | GPIO5 |

购买成品时请和卖家确认带 CH340 芯片、支持自行烧录。

## 快速开始

1. 克隆工程时请使用 `git clone --recursive <仓库地址>`（或克隆后执行 `git submodule update --init`）。公共库 [`sd2-common`](https://github.com/Joee-D/sd2-common) 会作为子模块出现在 `lib/sd2-common`，构建时自动编译，无需额外操作。
2. 用 VS Code 打开本工程（已安装 PlatformIO 插件）。
3. 复制 [`src/config.example.h`](src/config.example.h) 为 `src/config.h`，然后编辑：
   ```bash
   cp src/config.example.h src/config.h
   ```
   - `WIFI_SSID` / `WIFI_PASSWORD`：你的 WiFi
   - `DEEPSEEK_API_KEY`：在 [platform.deepseek.com/api_keys](https://platform.deepseek.com/api_keys) 创建，`sk-` 开头
   - `POLL_INTERVAL_MS`：刷新间隔（默认 60 秒）
   - `BRIGHTNESS`：屏幕亮度（0~1023，默认 800）
4. USB 连接小电视，点击 VS Code 底部 PlatformIO 工具栏的 **Upload**（或终端 `pio run -t upload`）。
5. 点击 **Serial Monitor**（波特率 921600）可查看日志。

## 目录结构

```
src/
├── main.cpp          主程序：TFT_eSPI 界面、WiFi、NTP、周期刷新
├── DeepSeekClient.h  HTTPS 余额查询（WiFiClientSecure + BearSSL）
├── cert.h            DigiCert Global Root G2 根证书（TLS 校验）
├── logo.h            DeepSeek 官方字标位图（左上角）
├── config.example.h  配置模板（复制为 config.h 后填写，config.h 不入库）
└── config.h          本地配置（.gitignore 忽略，不会提交）
images/               屏幕效果图
lib/sd2-common/       公共库子模块：WiFi 连接/校时/休眠/背光/HTTP/格式化
```

> 与 [`sd2-openwrt-traffic`](https://github.com/Joee-D/sd2-openwrt-traffic) 共用的基础功能（WiFi、NTP、定时休眠、背光、HTTP、格式化）已提炼到 [`sd2-common`](https://github.com/Joee-D/sd2-common)，本工程只保留 DeepSeek 余额相关的界面与请求逻辑。

## 工作原理

1. 开机显示连接页，连接 WiFi。
2. 通过 NTP 同步系统时间（ESP8266 无 RTC，校验证书前必须有正确时间）。
3. 用 WiFiClientSecure 建立 TLS 连接，校验 DigiCert 根证书，请求 `GET /user/balance`。
4. 解析返回的 `is_available` / `total_balance` / `granted_balance` / `topped_up_balance` 并绘制。
5. 每 60 秒自动刷新（请求期间临时关闭软看门狗，避免 TLS 阻塞导致复位）。

## 常见问题

**1. 显示 “Network error”**

设备连不上 `api.deepseek.com:443`。确认 WiFi 能上网，看串口日志中 `TLS connect failed` 的具体 SSL 错误。若确为证书链异常，可把 `config.h` 中 `VERIFY_TLS_CERT` 改为 `0`（不推荐长期使用）。

**2. 显示 “Bad API key”**

`config.h` 里的 Key 填错或已失效，重新到 DeepSeek 平台生成。

**3. 屏幕花屏 / 无显示**

- 确认是 ST7789 240×240（SD2 标准配置）；屏幕驱动与引脚已固化在 [`sd2-common/platformio/tft_setup.h`](https://github.com/Joee-D/sd2-common)，仅当硬件不同时才需要改。
- 背光亮度：`config.h` 中 `BRIGHTNESS`（0~1023）。

**4. 烧录失败**

- 确认数据线是数据线（不是纯充电线）。
- 上传波特率默认 921600，失败可改为 `upload_speed = 115200`。

## 参考

- 硬件开源：https://oshwhub.com/Q21182889/esp-xiao-dian-shi
- 固件参考：https://github.com/Jason6111/sd2
- 余额接口：https://api-docs.deepseek.com/zh-cn/api/get-user-balance
