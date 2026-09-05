/**
 * AI状态看板 — ESP32-2432S028R (CYD) AI 工作流状态显示器
 *
 * 通过 2.8 寸全屏大色块 + 状态文字显示 AI 工作流的运行状态。
 * MQTT 协议与原 project pi-workflow-status-light 完全兼容（仅 client_id 不同）：
 *   ai/status       — 监听工作流状态（JSON: {"state":"running", "message":"..."}）
 *   ai/led/command  — 直接控制（"red" / "green" / "blue" / "blink:yellow" / "breath:purple"）
 *   ai/status       — 发布心跳（{"state":"heartbeat"}）
 *
 * 配置文件（SPIFFS）:
 *   /config.json — WiFi/MQTT 配置参数
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "Config.h"
#include "StateMachine.h"
#include "DisplayPanel.h"

// ====== 全局对象 ======
StateMachine g_stateMachine;
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
unsigned long g_lastHeartbeatMs = 0;
unsigned long g_lastMqttReconnectMs = 0;
unsigned long g_lastWifiReconnectMs = 0;
bool g_wifiConnected = false;
bool g_mqttConnected = false;
bool g_initialStateLoaded = false;
bool g_offlineMode = false;
int g_mqttConnectFailCount = 0;
bool g_criticalTriggered = false;

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

// ====== WiFi 连接 ======
void connectWiFi() {
    Serial.print("[WiFi] 正在连接 ");
    Serial.print(g_config.wifiSsid);
    Serial.print(" ... ");

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_config.wifiSsid, g_config.wifiPassword);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" 已连接!");
        Serial.printf("[WiFi] IP 地址: %s\n", WiFi.localIP().toString().c_str());
        g_wifiConnected = true;
        g_offlineMode = false;
        g_stateMachine.setBrightnessMultiplier(255);
        g_display.setNetworkStatus(true, g_mqttConnected);
    } else {
        Serial.println(" 超时失败!");
        g_wifiConnected = false;
        g_display.setNetworkStatus(false, false);
    }
}

// ====== MQTT 回调 ======
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.printf("[MQTT] 收到消息 | Topic: %s | Payload: %s\n", topic, message.c_str());

    // 处理 ai/led/command — 直接控制命令
    if (String(topic) == TOPIC_COMMAND) {
        message.trim();

        String effectType = "solid";
        String colorName = message;

        int colonPos = message.indexOf(':');
        if (colonPos > 0) {
            effectType = message.substring(0, colonPos);
            colorName = message.substring(colonPos + 1);
        }

        RGBColor color = StateMachine::colorNameToRGB(colorName);
        LEDEffect effect;

        if (effectType == "blink") {
            effect.type = EffectType::BLINK;
            effect.color1 = color;
            effect.periodMs = 500;
        } else if (effectType == "breath") {
            effect.type = EffectType::BREATH;
            effect.color1 = color;
            effect.periodMs = 2000;
        } else if (effectType == "alternate") {
            effect.type = EffectType::ALTERNATE;
            effect.color1 = color;
            effect.color2 = RGBColor::Black;
            effect.periodMs = 500;
        } else if (effectType == "chase") {
            effect.type = EffectType::CHASE;
            effect.color1 = color;
            effect.color2 = RGBColor(40, 40, 40);
            effect.periodMs = 3600;
        } else {
            effect.type = EffectType::SOLID;
            effect.color1 = color;
        }

        effect.brightness = 255;
        g_display.setEffect(effect);
        g_display.setStateName("cmd");   // 看板徽章显示 CMD，颜色来自命令
        Serial.printf("[LED] 直接控制: %s → %s\n", effectType.c_str(), colorName.c_str());
        return;
    }

    // 处理 ai/status — 工作流状态
    if (String(topic) == TOPIC_STATUS) {
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

        // 心跳消息不处理
        if (strcmp(state, "heartbeat") == 0) {
            return;
        }

        // 离线状态不改变显示（已在离线模式中处理）
        if (strcmp(state, "offline") == 0) {
            return;
        }

        g_initialStateLoaded = true;

        WorkflowState ws = StateMachine::stringToState(state);
        g_stateMachine.forceSetState(ws);
        g_display.setEffect(g_stateMachine.getCurrentEffect());
        g_display.setStateName(StateMachine::stateToString(ws));

        // 透传 message 字段到看板
        if (doc["message"]) {
            g_display.setMessage((const char*)doc["message"]);
        } else {
            g_display.setMessage("");
        }

        g_criticalTriggered = false;
        g_stateMachine.printState(Serial);
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
        g_mqttClient.subscribe(TOPIC_COMMAND, 1);
        Serial.printf("[MQTT] 订阅: %s (QoS 1)\n", TOPIC_COMMAND);

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

// ====== 进入离线模式 ======
void enterOfflineMode() {
    if (!g_offlineMode) {
        g_offlineMode = true;
        Serial.println("[系统] 进入离线模式（屏幕变暗）");
        g_display.setDimmed(true);
    }
}

// ====== 退出离线模式 ======
void exitOfflineMode() {
    if (g_offlineMode) {
        g_offlineMode = false;
        Serial.println("[系统] 退出离线模式");
        g_display.setDimmed(false);
    }
}

// ====== 进入严重故障状态 ======
void enterCriticalState() {
    if (!g_criticalTriggered) {
        g_criticalTriggered = true;
        Serial.println("[系统] 严重故障 — 红蓝交替闪烁");
        g_stateMachine.setState(WorkflowState::CRITICAL);
        g_display.setEffect(g_stateMachine.getCurrentEffect());
        g_display.setStateName("critical");
    }
}

// ====== 设置函数 ======
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================");
    Serial.println("  AI 状态看板 (CYD)");
    Serial.println("========================================");

    // 初始化屏幕看板
    g_display.begin();
    g_stateMachine.setState(WorkflowState::INIT);
    g_display.setEffect(g_stateMachine.getCurrentEffect());
    g_display.setStateName(StateMachine::stateToString(WorkflowState::INIT));
    g_stateMachine.printState(Serial);

    // 加载配置文件
    loadConfig();

    // 连接 WiFi
    connectWiFi();

    // 连接 MQTT
    if (g_wifiConnected) {
        connectMQTT();
    }

    // 如果 MQTT 未连接，进入空闲
    if (!g_mqttConnected) {
        Serial.println("[MQTT] 未连接，进入空闲模式（等待重连）");
        g_stateMachine.setState(WorkflowState::IDLE);
        g_display.setEffect(g_stateMachine.getCurrentEffect());
        g_display.setStateName(StateMachine::stateToString(WorkflowState::IDLE));
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
            connectWiFi();
        }
    } else {
        if (!g_wifiConnected) {
            g_wifiConnected = true;
            Serial.println("[WiFi] 已重新连接");
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
        g_stateMachine.setState(WorkflowState::IDLE);
        g_display.setEffect(g_stateMachine.getCurrentEffect());
        g_display.setStateName(StateMachine::stateToString(WorkflowState::IDLE));
        g_initialStateLoaded = true;
    }

    // ====== 更新状态机与屏幕 ======
    g_stateMachine.update();
    g_display.update();

    delay(10);
}