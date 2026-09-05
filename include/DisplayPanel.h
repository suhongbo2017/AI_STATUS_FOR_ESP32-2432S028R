#ifndef DISPLAY_PANEL_H
#define DISPLAY_PANEL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "StatusRegistry.h"

// 屏幕看板 UI（320x240 横屏）：
//   底色纯黑；上下各 12% 棕黄色边框：
//     上：日期 + 时间；下：WiFi/MQTT 连接状态
//   中间状态显示区（黑底）：
//     状态大字 + 描述 + 消息，按状态配专属动效：
//       init 紫 / idle 蓝 / done 绿（静态）
//       running 黄：流水动画
//       waiting 青：呼吸闪烁
//       cmd 橙红：涟漪扩散
//       其余状态静态（error 红 / critical 品红 / throttled 橙 / unknown 灰）
// 状态/颜色/文案来自 StatusRegistry；Sprite 离屏渲染 + 区域化重绘防闪烁。
// 板载 RGB LED（绿/蓝）做在线/离线指示（GPIO4 红灯与 TFT_RST 共用，不驱动）
class DisplayPanel {
public:
    DisplayPanel() : m_text(&m_tft), m_flow(&m_tft), m_ripple(&m_tft) {}

    void begin();
    void setState(const String& key, const String& message);
    void setNetworkStatus(bool wifiOk, bool mqttOk);
    void setDimmed(bool dimmed);
    void update();

private:
    void renderAll();
    void renderChanged();            // 状态切换：推变化 Sprite，避免整屏闪烁
    void renderFrame();              // 上下棕黄边框 + 日期时间（全量）
    void renderText(uint32_t now);   // 中间文字区 sprite（含 waiting 闪烁亮度）
    void renderFlow(uint32_t now);   // running 流水带
    void renderRipple(uint32_t now); // cmd 涟漪
    void renderProgressBar(uint32_t now, bool clearOnly);
    void renderStatusIndicators();
    void refreshClock();
    uint16_t dim(uint16_t color565) const;
    uint16_t rgb565(const RGBColor& c) const;
    uint16_t rgb565l(const RGBColor& c, float lum) const;  // 带亮度
    bool isRunning() const;
    bool isWaiting() const;
    bool isCmd() const;

    TFT_eSPI m_tft;
    TFT_eSprite m_text;    // 中间文字区
    TFT_eSprite m_flow;    // 流水动画带
    TFT_eSprite m_ripple;  // 涟漪动画
    bool m_dimmed = false;
    bool m_wifiOk = false;
    bool m_mqttOk = false;
    const StateDef* m_state = nullptr;
    String m_message;
    uint32_t m_lastClockMs = 0;
    uint32_t m_lastAnimMs = 0;
    bool m_needFullRedraw = true;
    bool m_needChangedRedraw = false;
};

#endif // DISPLAY_PANEL_H