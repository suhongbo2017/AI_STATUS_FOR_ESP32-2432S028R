# AI 状态看板 — ESP32 AI 工作流状态显示器

基于 **ESP32-2432S028R (CYD)** 的 Web 看板设备，通过 MQTT 接收 AI Agent 工作流状态并在 2.8" TFT 屏幕上实时渲染。

## 快速上手

### 硬件清单

| 项目 | 规格 |
|------|------|
| 主控 | ESP32-DevKitC (WROOM-32) |
| 屏幕 | 2.8" ST7789, 320×240 SPI |
| 板载 LED | 绿(16) / 蓝(17)，共阳极 |
| 供电 | USB Type-C 5V |

### 首次烧录

```bash
# 1. 安装 PlatformIO CLI: https://platformio.org/install

# 2. 克隆仓库 + 进入目录
git clone https://github.com/suhongbo2017/AI_STATUS_FOR_ESP32-2432S028R.git
cd AI_STATUS_FOR_ESP32-2432S028R

# 3. 上传固件（替换 COM5 为你的串口号）
pio run -t upload --upload-port COM5

# 4. （可选）烧录 SPIFFS 数据分区 —— 必须做！否则 config.json 不存在会导致 NTP/MQTT 失败
pio run -t uploadfs

# 5. 查看串口日志
pio device monitor -p COM5 -b 115200
```

首次启动期望看到的日志：

```
[屏幕] 大字区 sprite 就绪 (heap free ~190 KB)
[SPIFFS] 配置文件加载成功
  WiFi SSID: JHS
  MQTT Host: broker.emqx.io:1883
[WiFi] 正在连接 JHS ...  已连接!
[WiFi] IP 地址: 192.168.102.21
[NTP] 已配置（阿里云 / pool.ntp.org / 腾讯）
[MQTT] 正在连接 Broker broker.emqx.io:1883 ...  已连接!
[MQTT] 订阅: ai/status (QoS 1)
[状态] idle | 
```

---

## 编译与上传

| 命令 | 说明 |
|------|------|
| `pio run` | 编译固件 |
| `pio run -t upload` | 编译 + 上传到 ESP32 |
| `pio run -t uploadfs` | 上传 data/ 目录到 SPIFFS 分区 |
| `pio run -t erase` | 擦除 Flash（恢复出厂） |
| `pio device monitor -p COMx -b 115200` | 打开串口监视器 |

**注意事项：**
- ESP32 需要在 **PlatformIO → Device → Enter Firmware Download Mode** 后按一次板载 BOOT 按钮才能识别
- Windows 需安装 CH340/CP2102 驱动
- 波特率固定 `115200`，停止位 `8-N-1`

---

## 工作流程

### 1. 启动流程

```
系统上电
    ├── loadConfig()     ← 读取 SPIFFS/config.json
    ├── connectWiFi(20s) ← 尝试连接配置中的 WiFi
    │   ├── 成功 → configTime(NTP) + connectMQTT()
    │   └── 失败 → 启动 SoftAP 热点 + DNS+HTTP 配网页面
    ├── MQTT 连接
    │   ├── 成功 → 订阅 ai/status + 恢复上次状态
    │   └── 连续 3 次失败 → 触发关键错误态（红底闪烁）
    └── loop() 循环开始
```

### 2. 运行时行为

| 事件 | 处理 |
|------|------|
| 收到 `ai/status` JSON | 解析 state→渲染，message→显示在中央大字下方 |
| 收到 `ai/led/command` | 直接控制板载 LED（red/green/blue/blink/breath） |
| WiFi 断开 | 每 5 秒重连；屏幕进入离线降级态（灰底） |
| MQTT 断开 | 每 10 秒重连；断连期间保持最后显示的状态 |
| NTP 未同步 | 每分钟重试；时间区域显示 `UP Xmin` 表示已运行时长 |
| 无消息 > 30 秒 | LED 灭 + 背光自动关闭（息屏模式） |
| 再次收到消息 | 唤醒背光、点亮 LED |

### 3. 自动息屏

超时 `SCREEN_TIMEOUT_MS`（默认 30s，可在 Config.h 修改）无活动后：
- 背光关闭（`TFT_BL = LOW`）
- 板载 LED 熄灭
- 内存中状态保持，下次 activity 瞬间唤醒

---

## 网络配置

### 方式一：data/config.json（推荐）

编辑 `data/config.json`，然后烧录：

```json
{
    "wifi": {
        "ssid": "你的WiFi名称",
        "password": "你的WiFi密码"
    },
    "mqtt": {
        "host": "broker.emqx.io",
        "port": 1883,
        "user": "",
        "pass": ""
    }
}
```

然后 `pio run -t uploadfs` 上传到 SPIFFS。

### 方式二：AP 热点配网

如果设备无法连接任何 WiFi（20 秒超时），自动进入 AP 模式：

1. 手机/电脑连接到热点 `AI-Status-XXXX`（XXXX = MAC 末两位大写）
2. 浏览器访问 `http://192.168.4.1`（DNS 劫持使任意域名均跳转到此页）
3. 填写 WiFi 名称、密码、MQTT 服务器地址和端口
4. 点击"保存并重启"
5. 配置保存到 SPIFFS，设备自动重启并联网

> 配置保存后 `onApSave()` 会写入 `/config.json` 并调用 `ESP.restart()`。

### 硬编码默认值

也可在 `include/Config.h` 修改默认 WiFi/MQTT/NTP 参数：

```cpp
#define WIFI_SSID       "MyHomeWiFi"
#define WIFI_PASSWORD   "mypassword"
#define MQTT_BROKER     "192.168.1.100"
#define MQTT_PORT       1883
#define MQTT_USER       "admin"
#define MQTT_PASS       "secret"
#define NTP_SERVER1     "ntp.aliyun.com"
#define NTP_SERVER2     "pool.ntp.org"
#define NTP_SERVER3     "ntp.tencent.com"
```

这些值仅作为 fallback —— 如果 SPIFFS 中存在有效的 `config.json`，则优先使用文件配置。

---

## MQTT 协议

### 发布 & 订阅

| Topic | 方向 | 说明 |
|-------|------|------|
| `ai/status` | 订阅 | 接收工作流状态 JSON |
| `ai/led/command` | 订阅 | 直接控制板载 LED |
| `ai/status` | 发布 | 心跳包（含 `"state":"heartbeat"`） |

### `ai/status` JSON 格式

```json
{"state": "<状态名>", "message": "描述文字"}
```

支持的内置状态及配色：

| state | 背景色 | 文字色 | 说明 |
|-------|--------|--------|------|
| `running` | 金色 (#FFD700) | 深灰 | 执行中，带充电进度条动效 |
| `waiting` | 天青 (#00CED1) | 白 | 等待中，呼吸闪烁 |
| `idle` | 天蓝 (#87CEEB) | 白 | 空闲，纯背景 |
| `done` | 翠绿 (#00FA9A) | 白 | 完成，纯背景 |
| `error` | 鲜红 (#DC143C) | 白 | 出错，纯背景 |
| `critical` | 暗红 (#8B0000) | 白 | 严重错误，交替闪烁 |
| *(其他)* | 灰色 (#787878) | 白 | 未知状态兜底 |

自定义新状态：在 `src/StatusRegistry.cpp` 末尾添加一行即可：
```cpp
// 状态名 | 16 进制 RRGGBB 或 RGB565 | 中文标签 | 文本字体大小 | 是否倒序
registry.registerState("deploying", 0xDAA520, "部署中", MID_TEXT_SIZE, false);
```

### `ai/led/command` 消息格式

纯字符串（Payload 为以下之一）：

| 值 | LED 效果 |
|----|----------|
| `"red"` | 红灯常亮（GPIO4，如可用） |
| `"green"` | 绿灯常亮（GPIO16） |
| `"blue"` | 蓝灯常亮（GPIO17） |
| `"blink:red"` | 红灯闪烁（1Hz） |
| `"breath:green"` | 绿灯呼吸 |

当前硬件限制：GPIO4 与 TFT_RST 共用，驱动红灯会复位屏幕。**仅绿/蓝 LED 可控。**

### 心跳

每 `HEARTBEAT_INTERVAL_MS`（默认 30s）向 `ai/status` 发布一次：

```json
{"state": "heartbeat"}
```

---

## 显示面板

### 布局（320×240 横屏）

```
┌─────────────────────────────┐
│ [白框 FRAME_H=30px]         │
│ 2025-09-07         14:30:25 │  ← 左上日期 + 右上时钟
│                             │
│                             │
│          [状态大字]           │  ← 中间区
│      [消息描述文字]           │     （纯色背景 + 中文）
│                             │
│                             │
├─────────────────────────────┤
│ [白框 FRAME_H=30px]         │
│ WiFi[M] MQTT[O] NTP[O]     │  ← 底部指示灯
│                OFFLINE      │  ← 息屏时出现
└─────────────────────────────┘
```

### 动效

- **running**: 底部绿色充电式进度条（格状，随时间递增）
- **waiting**: 全框呼吸闪烁（背景色明暗交替）
- **idle/done/error/critical**: 静态填充（无动画）

### Sprite 离屏渲染

为避免逐字闪屏，大字区和消息文字使用 `TFT_eSprite` 离屏渲染——先在内存中绘制整块内容，再一次性推屏。内存不足时自动回退直接绘制。

---

## 故障排查

### 问题 | 症状 | 解决方法
|------|------|---------|
| SPIFFS config.json 丢失 | 串口打印 `[SPIFFS] 配置文件 /config.json 不存在` | `pio run -t uploadfs` |
| MQTT hostname 为空 | 串口打印 `[MQTT] 连接失败` 且 host 为空 | 同上，确保 config.json 存在 |
| NTP 永远不同步 | 屏幕显示 `UP Xmin`，底栏 NTP 红灯 | WiFi 未连接时 configTime 无效；检查网络连接 |
| WiFi 连接超时 | 进入 AP 热点模式 | 用热点配网重新填入正确 WiFi |
| 屏幕不亮 / 黑屏 | 上电后无任何显示 | 检查 Type-C 供电 ≥ 1A；BOOT 键是否正常进入下载模式 |
| 编译报错 `ST7789 not defined` | 平台 IO 工具链缺失 | `pio lib install bodmer/TFT_eSPI@^2.5.43` |
| 上传失败 `esp.py` | 未进入下载模式 | 按住 BOOT → 点击下载 → 松开 BOOT |

### 恢复出厂设置

```bash
pio run -t erase      # 擦除整个 Flash
pio run -t uploadfs   # 重建 config.json
```

### 多设备共存

同一局域网内可部署多台设备：
- 每台有唯一 MAC，AP 热点名不同（`AI-Status-{MAC尾}`）
- MQTT client_id 固定为 `ai-status-display`——如需区分，修改 `Config.h` 中的 `MQTT_CLIENT_ID`

---

## 源码结构

```
├── src/
│   ├── main.cpp          # 入口、WiFi/MQTT/NTP 管理、loop 主循环
│   ├── DisplayPanel.cpp  # 屏幕渲染引擎（边框/状态/动效）
│   ├── StatusRegistry.cpp # 状态定义与配色注册表
│   └── ApConfig.cpp      # SoftAP + DNS + HTTP 配网页
├── include/
│   ├── Config.h          # 全局常量（WiFi/MQTT/NTP/引脚）
│   ├── DisplayPanel.h    # 屏幕类接口
│   ├── StatusRegistry.h
│   └── ApConfig.h
├── data/
│   └── config.json       # 初始 WiFi+MQTT 配置（上传到 SPIFFS）
├── platformio.ini        # PlatformIO 工程配置
└── README.md             # 本文件
```

---

## 依赖库

| 库 | 版本 | 用途 |
|----|------|------|
| knolleary/PubSubClient | ^2.8 | MQTT 客户端 |
| bblanchon/ArduinoJson | ^7.3 | JSON 序列化/反序列化 |
| bodmer/TFT_eSPI | ^2.5.43 | ST7789/LCD 驱动 |

---

## License

ISC
