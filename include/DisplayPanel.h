#ifndef DISPLAY_PANEL_H
#define DISPLAY_PANEL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "StateMachine.h"

// 火柴人姿势（状态表达）
enum class FigurePose : uint8_t {
    STAND,  // 站立：空闲/手动
    WALK,   // 行走：初始化/限流
    RUN,    // 跑动：运行中
    SIT,    // 坐姿休息：完成
    LIE,    // 躺倒：出错/严重故障
    WAVE    // 站立挥手：等待输入
};

// 状态 → 中文/姿势 映射项
struct StateInfo {
    const char* key;          // 大写内部名
    const char* cnName;       // 中文状态名（24px 大字/卡片词）
    const char* cnDesc;       // 中文描述
    FigurePose pose;          // 火柴人姿势
};

// 屏幕看板 UI（320x240 横屏，深色仪表盘风格）：
//   顶栏：中文标题 + 状态色圆点 + 时钟
//   中部：左侧状态卡片（状态色 + 火柴人动画 + 中文状态词）
//         右侧中文状态大字 + 描述 + 消息
//   running 时：细进度条动画
//   底栏：WiFi/MQTT/NTP 指示灯 + 离线提示
// 通过 Sprite 离屏渲染避免切换闪烁。
// 板载 RGB LED（绿/蓝）做在线/离线指示（GPIO4 红灯与 TFT_RST 共用，不驱动）
class DisplayPanel {
public:
    DisplayPanel() : m_card(&m_tft), m_text(&m_tft) {}

    void begin();
    void setEffect(const LEDEffect& effect);   // 驱动成颜色
    void setStateName(const String& name);     // 内部名如 "running"
    void setMessage(const String& message);
    void setNetworkStatus(bool wifiOk, bool mqttOk);
    void setDimmed(bool dimmed);
    void update();

private:
    static const StateInfo* lookupState(const String& upperName);
    const StateInfo* currentState() const;

    void renderAll();
    void renderTopBar();
    void renderCard(uint32_t now);     // 卡片 sprite（含火柴人）
    void renderTextPanel();            // 右侧文字 sprite
    void renderProgressBar(uint32_t now, bool clearOnly);
    void renderStatusBar();
    void refreshClock();
    void drawFigure(const StateInfo& info, uint32_t now);
    uint16_t dim(uint16_t color565) const;
    uint16_t rgb565(const RGBColor& c) const;

    TFT_eSPI m_tft;
    TFT_eSprite m_card;    // 140x140 状态卡片
    TFT_eSprite m_text;    // 右侧文字区
    bool m_dimmed = false;
    bool m_wifiOk = false;
    bool m_mqttOk = false;
    String m_stateName = "INIT";
    String m_message;
    RGBColor m_statusColor = RGBColor::Purple;
    uint32_t m_lastAnimMs = 0;
    uint32_t m_lastClockMs = 0;
    bool m_needFullRedraw = true;
};

#endif // DISPLAY_PANEL_H