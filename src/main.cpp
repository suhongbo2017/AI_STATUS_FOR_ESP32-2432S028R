/**
 * AI状态看板 — ESP32-2432S028R (CYD) AI 工作流状态显示器
 *
 * MQTT 协议与原项目 pi-workflow-status-light 完全兼容（仅 client_id 不同）：
 *   ai/status       — 监听工作流状态（JSON: {"state":"running", "message":"..."}）
 *   ai/led/command  — 直接控制（"red" / "green" / "blue" / "blink:yellow" / "breath:purple"）
 *   ai/status       — 发布心跳（{"state":"heartbeat"}）
 *
 * 状态定义与配色统一由 StatusRegistry 管理，新增状态只需在注册表加一行。
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "Config.h"
#include "StatusRegistry.h"
#include "DisplayPanel.h"
#include "ApConfig.h"

// ====== 全局对象 ======
DisplayPanel g_display;
WiFiClient g_wifiClient;
PubSubClient g_mqttClient(g_wifiClient);

// ====== 运行时配置（从文件读取或硬编码默认） ======
struct RuntimeConfig {
    char wifiSsid[32] = WIFI_SSID;
    char wifiPassword[64] = WIFI_PASSWORD;
    char mqttBroker[64] = MQTT_BROKER;
    uint16_t mqttPort = MQTT_PORT;
    char mqttUser[32] = MQTT_USER;
    char mqttPass[32] = MQTT_PASS;
} g_config;

// ====== 状态变量 ======
String g_stateKey = "init";
unsigned long g_lastHeartbeatMs = 0;
unsigned long g_lastMqttReconnectMs = 0;
unsigned long g_lastWifiReconnectMs = 0;
bool g_wifiConnected = false;
bool g_mqttConnected = false;
bool g_initialStateLoaded = false;
bool g_offlineMode = false;
int g_mqttConnectFailCount = 0;
bool g_criticalTriggered = false;

// ====== 应用状态到看板 ======
void applyState(const char* key, const char* msg) {
    g_stateKey = key;
    g_display.setState(key, msg ? msg : "");
    Serial.printf("[状态] %s%s%s\n", key, msg ? " | " : "", msg ? msg : "");
}

// ====== 配置文件读写 ======
bool loadConfig() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[SPIFFS] 挂载失败，使用默认配置");
        return false;
    }

    if (!SPIFFS.exists("/config.json")) {
        Serial.println("[SPIFFS] 配置文件 /config.json 不存在，使用默认配置");
        return false;
    }

    File file = SPIFFS.open("/config.json", "r");
    if (!file) {
        Serial.println("[SPIFFS] 打开配置文件失败，使用默认配置");
        return false;
    }

    String content = file.readString();
    file.close();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        Serial.printf("[SPIFFS] 配置文件解析失败: %s\n", error.c_str());
        return false;
    }

    if (doc["wifi"]["ssid"]) {
        strlcpy(g_config.wifiSsid, doc["wifi"]["ssid"], sizeof(g_config.wifiSsid));
    }
    if (doc["wifi"]["password"]) {
        strlcpy(g_config.wifiPassword, doc["wifi"]["password"], sizeof(g_config.wifiPassword));
    }
    if (doc["mqtt"]["host"]) {
        strlcpy(g_config.mqttBroker, doc["mqtt"]["host"], sizeof(g_config.mqttBroker));
    }
    if (doc["mqtt"]["port"]) {
        g_config.mqttPort = doc["mqtt"]["port"];
    }
    if (doc["mqtt"]["user"]) {
        strlcpy(g_config.mqttUser, doc["mqtt"]["user"], sizeof(g_config.mqttUser));
    }
    if (doc["mqtt"]["pass"]) {
        strlcpy(g_config.mqttPass, doc["mqtt"]["pass"], sizeof(g_config.mqttPass));
    }

    Serial.println("[SPIFFS] 配置文件加载成功");
    Serial.printf("  WiFi SSID: %s\n", g_config.wifiSsid);
    Serial.printf("  MQTT Host: %s:%d\n", g_config.mqttBroker, g_config.mqttPort);
    return true;
}

// ====== WiFi 连接（阻塞等待 timeoutMs；失败返回 false）======
bool connectWiFi(unsigned long timeoutMs) {
    Serial.print("[WiFi] 正在连接 ");
    Serial.print(g_config.wifiSsid);
    Serial.print(" ... ");

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_config.wifiSsid, g_config.wifiPassword);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" 已连接!");
        Serial.printf("[WiFi] IP 地址: %s\n", WiFi.localIP().toString().c_str());
        g_wifiConnected = true;
        g_offlineMode = false;
        g_display.setNetworkStatus(true, g_mqttConnected);
        return true;
    }
    Serial.println(" 超时失败!");
    g_wifiConnected = false;
    g_display.setNetworkStatus(false, false);
    return false;
}

// ====== AP 配网：保存配置到 SPIFFS 后重启 ======
void onApSave(const String& ssid, const String& pass,
              const String& host, const String& port) {
    File f = SPIFFS.open("/config.json", "w");
    if (f) {
        JsonDocument doc;
        doc["wifi"]["ssid"] = ssid;
        doc["wifi"]["password"] = pass;
        doc["mqtt"]["host"] = host;
        doc["mqtt"]["port"] = port.toInt();
        serializeJson(doc, f);
        f.close();
        Serial.println("[配网] 配置已保存到 SPIFFS");
    } else {
        Serial.println("[配网] ERROR: 保存配置失败");
    }
    delay(800);
    ESP.restart();
}

// ====== MQTT 回调 ======
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.printf("[MQTT] 收到消息 | Topic: %s | Payload: %s\n", topic, message.c_str());

    // 处理 ai/led/command — 直接控制命令（看板统一橙红涟漪"执行命令"态）
    if (String(topic) == TOPIC_COMMAND) {
        message.trim();

        String colorName = message;
        int colonPos = message.indexOf(':');
        if (colonPos > 0) {
            colorName = message.substring(colonPos + 1);
        }

        g_display.setState("cmd", colorName);
        Serial.printf("[LED] 直接控制: %s\n", colorName.c_str());
        return;
    }

    // 处理 ai/status — 工作流状态
    if (String(topic) == TOPIC_STATUS) {
        // 任何状态消息都算活动：刷新息屏计时并唤醒
        g_display.notifyActivity();

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, message);

        if (error) {
            Serial.printf("[MQTT] JSON 解析失败: %s\n", error.c_str());
            return;
        }

        const char* state = doc["state"];
        if (state == nullptr) {
            Serial.println("[MQTT] 消息中没有 state 字段");
            return;
        }

        // 心跳/离线消息不改变显示
        // 注意：Pi 扩展心跳格式为 {"state":"idle","message":"keepalive"}，
        // 需同时识别 message==keepalive，否则等待态下消息文本会反复跳变
        const char* msg = doc["message"] ? (const char*)doc["message"] : "";
        if (strcmp(state, "heartbeat") == 0 || strcmp(state, "offline") == 0
            || strcmp(msg, "keepalive") == 0) {
            return;
        }

        g_initialStateLoaded = true;
        g_criticalTriggered = false;

        applyState(state, msg);
    }
}

// ====== MQTT 连接 ======
bool connectMQTT() {
    Serial.print("[MQTT] 正在连接 Broker ");
    Serial.print(g_config.mqttBroker);
    Serial.print(":");
    Serial.print(g_config.mqttPort);
    Serial.print(" ... ");

    g_mqttClient.setServer(g_config.mqttBroker, g_config.mqttPort);
    g_mqttClient.setCallback(mqttCallback);

    String lwtPayload = "{\"state\":\"offline\"}";

    bool connected = false;
    if (strlen(g_config.mqttUser) > 0) {
        connected = g_mqttClient.connect(MQTT_CLIENT_ID,
                                         g_config.mqttUser, g_config.mqttPass,
                                         TOPIC_STATUS, 1, true, lwtPayload.c_str());
    } else {
        connected = g_mqttClient.connect(MQTT_CLIENT_ID, NULL, NULL,
                                         TOPIC_STATUS, 1, true, lwtPayload.c_str());
    }

    if (connected) {
        Serial.println(" 已连接!");
        g_mqttConnected = true;
        g_mqttConnectFailCount = 0;
        g_criticalTriggered = false;
        g_offlineMode = false;
        g_display.setNetworkStatus(g_wifiConnected, true);

        g_mqttClient.subscribe(TOPIC_STATUS, 1);
        Serial.printf("[MQTT] 订阅: %s (QoS 1)\n", TOPIC_STATUS);
        // 注意：不订阅 ai/led/command——那是灯条设备的命令通道，
        // 原 Pi 扩展在状态变化时会发 chase:yellow 等命令，会覆盖看板状态
        // （cmd 状态仍可通过 ai/status 的 {"state":"cmd"} 触发）

        String onlinePayload = "{\"state\":\"idle\"}";
        g_mqttClient.publish(TOPIC_STATUS, onlinePayload.c_str(), true);

        return true;
    } else {
        Serial.printf(" 失败! (rc=%d)\n", g_mqttClient.state());
        g_mqttConnected = false;
        g_mqttConnectFailCount++;
        g_display.setNetworkStatus(g_wifiConnected, false);
        return false;
    }
}

// ====== 发布心跳 ======
void publishHeartbeat() {
    if (!g_mqttClient.connected()) return;

    char payload[32];
    snprintf(payload, sizeof(payload), "{\"state\":\"heartbeat\"}");
    bool ok = g_mqttClient.publish(TOPIC_HEARTBEAT, payload, false);
    if (ok) {
        Serial.println("[MQTT] 心跳已发送");
    } else {
        Serial.printf("[MQTT] 心跳发送失败 rc=%d\n", g_mqttClient.state());
    }
}

// ====== 离线/恢复 ======
void enterOfflineMode() {
    if (!g_offlineMode) {
        g_offlineMode = true;
        Serial.println("[系统] 进入离线模式（屏幕变暗）");
        g_display.setDimmed(true);
    }
}

void exitOfflineMode() {
    if (g_offlineMode) {
        g_offlineMode = false;
        Serial.println("[系统] 退出离线模式");
        g_display.setDimmed(false);
    }
}

// ====== MQTT 连续失败 → 严重故障 ======
void enterCriticalState() {
    if (!g_criticalTriggered) {
        g_criticalTriggered = true;
        Serial.println("[系统] 严重故障 — MQTT 连接失败");
        applyState("critical", "MQTT 连接失败");
    }
}

// ====== 设置函数 ======
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================");
    Serial.println("  AI 状态看板 (CYD)");
    Serial.println("========================================");

    g_display.begin();
    applyState("init", "系统启动中");

    loadConfig();

    // 尝试连接 WiFi：失败（新网络/无配置/密码变更）则进入 AP 配网模式
    if (!connectWiFi(20000)) {
        Serial.println("[配网] WiFi 连接失败，启动 AP 配网模式");
        Serial.println("[配网] 用手机连接热点，浏览器访问 http://192.168.4.1 完成配置");
        g_display.setState("waiting", "");  // 青色等待输入
        startApConfig("AI-Status",
                      g_config.wifiSsid, g_config.wifiPassword,
                      g_config.mqttBroker, String(g_config.mqttPort).c_str(),
                      onApSave,
                      []() {
                          // 配网期间驱动屏幕刷新并保活（不下线、不息屏）
                          g_display.notifyActivity();
                          g_display.update();
                      });
        // 配置保存成功后在 onApSave 中重启，不会执行到这里
    }

    if (g_wifiConnected) {
        configTime(NTP_GMT_OFFSET_SEC, 0, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
        Serial.println("[NTP] 已配置（阿里云 / pool.ntp.org / 腾讯）");
        connectMQTT();
    }

    // 如果 MQTT 未连接，进入空闲等待重连
    if (!g_mqttConnected) {
        Serial.println("[MQTT] 未连接，进入空闲模式（等待重连）");
        applyState("idle", "等待重连");
    }

    g_lastHeartbeatMs = millis();
    g_lastMqttReconnectMs = millis();
    g_lastWifiReconnectMs = millis();
}

// ====== 主循环 ======
void loop() {
    unsigned long now = millis();

    // ====== WiFi 连接管理 ======
    if (WiFi.status() != WL_CONNECTED) {
        if (g_wifiConnected) {
            Serial.println("[WiFi] 连接断开!");
            g_wifiConnected = false;
            g_mqttConnected = false;
            enterOfflineMode();
        }

        if (now - g_lastWifiReconnectMs >= 5000) {
            g_lastWifiReconnectMs = now;
            connectWiFi(10000);
        }
    } else {
        if (!g_wifiConnected) {
            g_wifiConnected = true;
            Serial.println("[WiFi] 已重新连接");
        }

        // ====== NTP 重试：同步前每分钟检查并重试一次 ======
        static unsigned long lastNtpRetryMs = 0;
        if (now - lastNtpRetryMs >= 60000) {
            lastNtpRetryMs = now;
            if (time(nullptr) < 1600000000) {
                configTime(NTP_GMT_OFFSET_SEC, 0, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
                Serial.println("[NTP] 未同步，重试中…");
            }
        }

        // ====== MQTT 连接管理 ======
        if (!g_mqttClient.connected()) {
            if (g_mqttConnected) {
                Serial.println("[MQTT] 连接断开!");
                g_mqttConnected = false;
                enterOfflineMode();
            }

            if (now - g_lastMqttReconnectMs >= 10000) {
                g_lastMqttReconnectMs = now;
                bool ok = connectMQTT();

                if (!ok && g_mqttConnectFailCount >= 3) {
                    enterCriticalState();
                }
            }
        } else {
            g_mqttClient.loop();

            if (!g_mqttConnected) {
                g_mqttConnected = true;
                g_mqttConnectFailCount = 0;
                g_criticalTriggered = false;
                exitOfflineMode();
                Serial.println("[MQTT] 已重新连接");
            }

            if (now - g_lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
                g_lastHeartbeatMs = now;
                if (g_mqttClient.connected()) {
                    publishHeartbeat();
                }
            }
        }
    }

    // ====== 超时降级：启动后 10 秒仍未收到状态消息 → idle ======
    if (!g_initialStateLoaded && g_wifiConnected && g_mqttConnected && (now > 10000)) {
        Serial.println("[系统] 未收到状态消息，进入空闲模式");
        applyState("idle", "等待任务");
        g_initialStateLoaded = true;
    }

    // ====== 更新看板 ======
    g_display.update();

    delay(10);
}