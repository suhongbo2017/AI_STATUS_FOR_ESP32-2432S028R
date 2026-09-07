#ifndef CONFIG_H
#define CONFIG_H

// ====== WiFi 配置 ======
// 初始硬编码，后期可通过 SPIFFS 配置文件覆盖
#define WIFI_SSID       "JHS"
#define WIFI_PASSWORD   "jhs16888"

// ====== MQTT 配置 ======
#define MQTT_BROKER     "broker.emqx.io"
#define MQTT_PORT       1883
#define MQTT_USER       ""
#define MQTT_PASS       ""
// 必须与原灯条固件不同，否则两设备会互相踢下线
#define MQTT_CLIENT_ID  "ai-status-display"

// ====== MQTT Topics ======
#define TOPIC_STATUS    "ai/status"         // 接收工作流状态
#define TOPIC_COMMAND   "ai/led/command"    // 直接控制命令
#define TOPIC_HEARTBEAT "ai/status"         // 心跳发布（同一 topic）

// ====== 心跳间隔 ======
#define HEARTBEAT_INTERVAL_MS 30000

// ====== NTP ======
#define NTP_SERVER1     "ntp.aliyun.com"
#define NTP_SERVER2     "pool.ntp.org"
#define NTP_SERVER3     "ntp.tencent.com"
#define NTP_GMT_OFFSET_SEC (8 * 3600)       // 东八区

// ====== 屏幕 ======
// CYD: ILI9341 240x320 SPI（引脚在 platformio.ini 的 TFT_eSPI build_flags 中定义）
// 横屏显示 320x240（面板倒装，竖屏正向为 R2，横屏正向为 R3）
#define SCREEN_ROTATION 3
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// ====== CYD 板载 RGB LED ======
// 低电平点亮（共阳极），GPIO4 与 TFT_RST 共用，驱动红灯会复位屏幕，故只使用绿/蓝
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN  17

#endif // CONFIG_H