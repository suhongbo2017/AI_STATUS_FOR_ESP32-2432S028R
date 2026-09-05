#ifndef DISPLAY_PANEL_H
#define DISPLAY_PANEL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "StatusRegistry.h"

// 屏幕看板 UI（320x240 横屏，深色仪表盘风格）：
//   顶栏：中文标题 + 状态色圆点 + 时钟
//   中部：左侧状态卡片（分类色块 + 中文状态词 + 分类小字）
//         右侧中文状态大字 + 描述 + 消息
//   running 时：细进度条动画（唯一动效）
//   底栏：WiFi/MQTT/NTP 指示灯 + 离线提示
// 状态/颜色/文案全部来自 StatusRegistry，新增状态无需改 UI。
// 通过 Sprite 离屏渲染 + 区域化重绘避免切换闪烁。
// 板载 RGB LED（绿/蓝）做在线/离线指示（GPIO4 红灯与 TFT_RST 共用，不驱动）
class DisplayPanel {
public:
    DisplayPanel() : m_card(&m_tft), m_text(&m_tft) {}

    void begin();
    // 设置状态（key 为 MQTT 状态名，未知自动兜底 UNKNOWN 灰色）
    void setState(const String& key, const String& message);
    // 手动命令覆盖：只改颜色与显示名（ai/led/command 兼容）
    void setCommandColor(uint16_t rgb565Color, const char* cmdName);
    void setNetworkStatus(bool wifiOk, bool mqttOk);
    void setDimmed(bool dimmed);
    void update();

private:
    void renderAll();
    void renderChanged();            // 状态切换：只推变化 Sprite，避免整屏闪烁
    void renderTopBar();
    void renderCard();               // 卡片 sprite（分类色 + 中文状态词）
    void renderTextPanel();          // 右侧文字 sprite
    void renderProgressBar(uint32_t now, bool clearOnly);
    void renderStatusBar();
    void refreshClock();
    uint16_t dim(uint16_t color565) const;
    uint16_t rgb565(const RGBColor& c) const;
    uint16_t stateColor() const;     // 当前状态色（支持命令覆盖）
    bool isRunning() const;

    TFT_eSPI m_tft;
    TFT_eSprite m_card;    // 140x140 状态卡片
    TFT_eSprite m_text;    // 右侧文字区
    bool m_dimmed = false;
    bool m_wifiOk = false;
    bool m_mqttOk = false;
    const StateDef* m_state = nullptr;   // 当前状态（来自注册表）
    String m_message;
    bool m_cmdOverride = false;          // 手动命令覆盖中
    uint16_t m_cmdColor = 0;
    uint32_t m_lastClockMs = 0;
    bool m_needFullRedraw = true;        // 全屏重绘（开机/变暗）
    bool m_needChangedRedraw = false;    // 仅状态变化区域重绘
};

#endif // DISPLAY_PANEL_H