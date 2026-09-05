#ifndef DISPLAY_PANEL_H
#define DISPLAY_PANEL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "StateMachine.h"

// 屏幕看板 UI（320x240 横屏，深色仪表盘风格）：
//   顶栏：标题 + 时钟
//   中部：左侧状态徽章（状态色圆角方块 + 状态缩写）｜右侧状态名 + 描述 + 消息
//   running 时：底部细进度条动画（唯一动效）
//   底栏：WiFi/MQTT/NTP 指示灯 + 运行时长
// 板载 RGB LED（绿/蓝）做在线/离线指示（GPIO4 红灯与 TFT_RST 共用，不驱动）
class DisplayPanel {
public:
    void begin();
    // 状态由主程序驱动：颜色/缩写来自 effect.color1 + stateName 映射
    void setEffect(const LEDEffect& effect);
    void setStateName(const String& name);   // 例如 "running"（内部转大写/缩写）
    void setMessage(const String& message);
    void setNetworkStatus(bool wifiOk, bool mqttOk);
    void setDimmed(bool dimmed);
    void update();

private:
    void renderDashboard();
    void renderTopBar();
    void renderBadge();
    void renderRightPanel();
    void renderProgressBar(uint32_t now);
    void renderStatusBar();
    void refreshClock();
    uint16_t dim(uint16_t color565) const;
    uint16_t rgb565(const RGBColor& c);
    String badgeText() const;
    const char* stateDescription() const;
    bool isMessagePrintable() const;

    TFT_eSPI m_tft;
    bool m_dimmed = false;
    bool m_wifiOk = false;
    bool m_mqttOk = false;
    String m_stateName = "INIT";
    String m_message;
    RGBColor m_statusColor = RGBColor::Purple;
    uint32_t m_lastFrameMs = 0;
    uint32_t m_lastClockMs = 0;
    bool m_needFullRedraw = true;
};

#endif // DISPLAY_PANEL_H