# AI 状态看板 — 工作流与系统架构说明

## 一、系统总览

```
┌──────────────┐              MQTT              ┌─────────────────┐
│   你的电脑    │     publish ai/status        │    ESP32 设备     │
│   (Pi 端)    │ ────────────────────────────→  │  (CYD 接收器)    │
│              │   单向消息推送                │                 │
│ paho-mqtt    │                               │  WiFi → Broker  │
│              │                               │  MQTT subscribe │
└──────────────┘                               └─────────────────┘
                                                   │
                                                    ▼
                                             ┌─────────────┐
                                             │  TFT 2.8"   │
                                             │  240x320    │
                                             │  实时渲染    │
                                             └─────────────┘
```

**核心设计原则：** 单向推送，Pi 只管发消息到 MQTT Broker，ESP32 被动监听并渲染屏幕。没有握手、没有轮询、没有回调需求。

---

## 二、MQTT 协议详解

### 2.1 Topic 列表

| Topic | 方向 | ESP32 行为 |
|-------|------|-----------|
| `ai/status` | **Pi → ESP32**（订阅） | 解析 JSON 渲染状态 |
| `ai/status` | **ESP32 → Pi**（发布） | 心跳包 / LWT 离线宣告 |
| `ai/led/command` | **Pi → ESP32**（不订阅） | 直接控制板载 LED |

### 2.2 `ai/status` JSON 格式

```json
{"state": "<状态名>", "message": "描述文字"}
```

| state | 含义 | 说明 |
|-------|------|------|
| `"running"` | 执行中 | 金色背景 + 充电进度条动画 |
| `"waiting"` | 等待中 | 青色背景 + 呼吸闪烁 |
| `"idle"` | 空闲 | 天蓝背景（仅当 message≠"keepalive"时才渲染） |
| `"done"` | 完成 | 翠绿背景 |
| `"error"` | 出错 | 鲜红背景 |
| `"critical"` | 严重故障 | 暗红背景交替闪烁（由 ESP32 自动生成） |
| `"cmd"` | 命令态 | 橙红涟漪（用于执行外部命令的视觉反馈） |
| `"init"` | 启动中 | 灰色兜底（上电显示） |
| *(其他)* | 未知状态 | 灰色兜底 |

**注意：** 原 Pi 工作流框架发出的心跳被扩展为 `{"state":"idle","message":"keepalive"}`，ESP32 在 `mqttCallback()` 第 201-206 行会跳过这类消息的渲染（避免覆盖当前显示的活跃状态），但仍会触发息屏计时唤醒。

### 2.3 `ai/led/command` 消息格式

纯文本字符串 Payload：

```
"red"      → 红灯
"green"    → 绿灯（GPIO16）
"blue"     → 蓝灯（GPIO17）
"blink:red" → 红灯闪烁（冒号后是颜色名，提取冒号后内容）
"breath:purple" → 紫色呼吸
```

### 2.4 ESP32 发出的心跳

每 30 秒（`HEARTBEAT_INTERVAL_MS`）向 `ai/status` 发布一次：

```json
{"state":"heartbeat"}
```

以及 LWT（Will 遗嘱）消息——连接时注册，断线时自动发出：

```json
{"state":"offline"}
```

---

## 三、ESP32 启动流程

```
上电
  ↓
Serial.begin(115200)          ← 串口初始化，输出调试日志
  ↓
g_display.begin()             ← 初始化屏幕，分配 Sprite 内存 (~190KB heap)
  ↓
applyState("init", "系统启动中") ← 显示灰色兜底页
  ↓
loadConfig()                  ← 读取 SPIFFS/config.json
  ├── 不存在 → 使用 Config.h 硬编码默认值
  └── 存在 → 解析 JSON 填充 RuntimeConfig
  ↓
connectWiFi(20000ms)          ← 尝试 STA 模式连接 WiFi
  ├── 成功 → 继续
  └── 失败 → startApConfig()（见第四节）
  ↓
configTime(NTP servers)       ← 同步时间（阿里云/pool.org/腾讯）
  ↓
connectMQTT()                 ← 连接 MQTT Broker
  ├── 成功 → 订阅 ai/status QoS 1，发布 {"state":"idle"} 宣告上线
  └── 失败 → applyState("idle", "等待重连")，loop 中每 10s 重试
  ↓
loop() 进入主循环              ← 见第五节
```

---

## 四、AP 配网流程

当 `connectWiFi()` 超时 20 秒未连接成功（首次启动 / 新网络 / 密码变更 / config.json 丢失）：

```
startApConfig()
  ├── WiFi.mode(WIFI_AP)         ← 切换为热点模式
  ├── WiFi.softAP("AI-Status-XXXX")  ← MAC 末两位大写区分多台设备
  ├── DNS server 启动（域名劫持 * → 192.168.4.1）
  ├── WebServer 监听 80 端口
  │   ├── GET /        → 返回 HTML 配置表单
  │   └── POST /save   → 写入 SPIFFS/config.json + ESP.restart()
  └── 阻塞循环                     ← dns.processNextRequest() + server.handleClient()
                                       期间每 100ms 调用 g_refreshFn() 保活屏幕
```

用户操作：

1. 手机/电脑连接热点 `AI-Status-XXXX`
2. 浏览器访问 `http://192.168.4.1`（或任何域名均被 DNS 劫持到此页面）
3. 填写 WiFi SSID/密码、MQTT Host/Port
4. 点"保存并重启"
5. ESP32 写入 `/config.json` → 重启 → 回到第二节启动流程

---

## 五、主循环 loop() 职责

```cpp
loop() {
    // 1. WiFi 连接管理
    if (!connected) {
        enterOfflineMode();            // 屏幕变暗
        if (now - lastReconnect >= 5s) connectWiFi(10s);  // 每 5 秒重连
    } else {
        // 2. NTP 重试（每分钟，直到 time(nullptr) > 1600000000）
        if (ntpFailed && now - lastNtpRetry >= 60s) configTime(...);

        // 3. MQTT 管理
        if (!mqttClient.connected()) {
            connectMQTT();
            if (failCount >= 3) enterCriticalState();  // 持续失败触发红色报警
        } else {
            mqttClient.loop();                    // 处理收到的订阅消息
            if (now - lastHeartbeat >= 30s) publishHeartbeat();
        }
    }

    // 4. 超时降级：启动 10s 内没收到任何状态消息 → idle
    if (!initialStateLoaded && wifiOK && mqttOK && now > 10s) {
        applyState("idle", "等待任务");
    }

    // 5. 更新看板
    g_display.update();        // 每秒刷新时钟 + 检测状态变化
}
```

---

## 六、MQTT 回调 `mqttCallback()` 处理顺序

```
收到消息
  ↓
notifyActivity()              ← 刷新息屏计时并唤醒背光
  ↓
topic == "ai/led/command" ?
  ├── 是 → 解析颜色名 → setState("cmd", colorName) → 返回
  └── 否：继续
  ↓
topic == "ai/status"
  ↓
解析 JSON
  ↓
msg.state == null ? → log error → 返回
  ↓
msg.state ∈ {"heartbeat", "offline"} OR msg.message == "keepalive" ?
  ├── 是 → 不改变显示 → 返回（但已唤醒过屏幕）
  └── 否：继续
  ↓
g_initialStateLoaded = true;
g_criticalTriggered = false;
applyState(msg.state, msg.message)   ← 最终渲染
```

---

## 七、显示面板渲染引擎

### 7.1 布局结构

```
┌─────────────────────────────┐  FRAME_H = 30px（上边框白色条）
│ 左上: 日期 (Font2)          │
│        右上: 时钟 (Font4)    │
│                             │
│                             │
│        [大字区 Sprite]       │  ← 离屏渲染，切换时一次推屏
│   [消息描述文字居中]         │
│                             │
│                             │
├─────────────────────────────┤  MID_BOT 分界
│ 左: WiFi[M]  MQTT[O]  NTP[O]|  FRAME_H = 30px（下边框白色条）
│                        OFFLINE│  息屏时显示
└─────────────────────────────┘
```

### 7.2 各状态的视觉表现

| 状态 | 背景色 | 文字色 | 动效 |
|------|--------|--------|------|
| `running` | `#FFD700` 金色 | 深灰 | 底部绿色充电式进度条 |
| `waiting` | `#00CED1` 天青 | 白 | 整框明暗呼吸 |
| `idle` | `#87CEEB` 天蓝 | 白 | 静态 |
| `done` | `#00FA9A` 翠绿 | 白 | 静态 |
| `error` | `#DC143C` 鲜红 | 白 | 静态 |
| `critical` | `#8B0000` 暗红 | 白 | 左右翻转交替闪烁 |
| `cmd` | 橙红 | 白 | 涟漪扩散 |
| 离线 | 灰色半透明 | — | setDimmed() 降低亮度 |

### 7.3 Sprite 离屏渲染

为避免逐字闪屏，大字区使用 `TFT_eSprite m_mid` 在 SRAM 中预渲染，完成后一次性 `pushSprite()` 推屏。内存不足时自动回退到直接绘制。

---

## 八、Pi 端配置步骤

### 8.1 环境准备

```bash
pip install paho-mqtt
```

### 8.2 Python 示例代码

```python
import paho.mqtt.client as mqtt
import json

def publish_state(state, message=""):
    """推送到 ESP32 看板"""
    client = mqtt.Client()
    client.connect("broker.emqx.io", 1883)
    payload = {"state": state, "message": message}
    client.publish("ai/status", json.dumps(payload))
    client.disconnect()

# 示例：工作流状态机
if task_succeeded:
    publish_state("running", "数据处理中...")
elif task_completed:
    publish_state("done", "已完成")
else:
    publish_state("error", str(error_msg))
```

### 8.3 集成到 Agent 框架

在你的 Agent 工作流结束位置加一行：

```python
# 伪代码
result = run_agent_workflow()
publish_state("running" if result else "error", str(result)[:50])
```

不需要后台守护进程、不需要配置文件、不需要定时轮询。**纯单向发布一次即可**。

---

## 九、错误恢复机制

| 故障场景 | 检测方式 | 恢复行为 |
|----------|---------|---------|
| WiFi 断开 | `WiFi.status() != WL_CONNECTED` | 立即 dim 屏幕，每 5s 尝试重连 |
| WiFi 重连成功 | `WL_CONNECTED` 恢复 | 退出 dim，恢复 MQTT 连接 |
| MQTT 断开 | `!client.connected()` | 进入 dim，每 10s 重连 |
| MQTT 连续失败 ≥3 次 | `g_mqttConnectFailCount >= 3` | `enterCriticalState()` 红底报警 + 文字提示 |
| SPIFFS config 丢失 | `!SPIFFS.exists("/config.json")` | 使用 Config.h 硬编码值作为 fallback |
| NTP 未同步 | `time(nullptr) < 1600000000` | 每分钟重试，时钟显示 `UP Xmin` |
| 长时间无状态消息 | `millis() > 10000` 且未收到 | 降级到 `idle("等待任务")` |
| 息屏超时 | `millis() - lastActivityMs >= 30s` | 关闭背光 + LED，下次消息瞬间唤醒 |

---

## 十、文件映射表

| 文件 | 职责 |
|------|------|
| `src/main.cpp` | 入口、WiFi/MQTT/NTP 管理、loop 主循环、回调处理 |
| `src/DisplayPanel.cpp` | 屏幕渲染引擎（边框/状态块/动效/Sprite） |
| `src/StatusRegistry.cpp` | 5 种内置状态 + 自定义状态的配色注册表 |
| `src/ApConfig.cpp` | SoftAP + DNS 劫持 + HTTP 表单配网页 |
| `include/Config.h` | 全局常量（WiFi/MQTT/NTP/引脚/尺寸） |
| `include/DisplayPanel.h` | 屏幕类接口 |
| `data/config.json` | WiFi+MQTT 初始配置（上传到 SPIFFS） |
| `platformio.ini` | 编译目标、依赖库、TFT_eSPI 引脚定义 |
