#ifndef DISPLAY_PANEL_H
#define DISPLAY_PANEL_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "StatusRegistry.h"

// 屏幕看板 UI（320x240 横屏）：
//   上下各 12% 纯白边框：上=日期+时间，下=WiFi/MQTT/NTP 连接状态
//   中间区整块填充状态标准纯色 + 中文大字/描述/消息
//   动效：running（黄）= 黑色流水波；waiting（青）= 整块呼吸闪烁
//   其余静态：等待蓝 / 完成绿 / 出错红 / 未知灰
// 状态定义来自 StatusRegistry（5 核心状态 + 别名合并 + 灰色兜底）
// 板载 RGB LED（绿/蓝）做在线/离线指示（GPIO4 红灯与 TFT_RST 共用，不驱动）
class DisplayPanel {
public:
    void begin();
    // 任何 ai/status 消息到来时调用：刷新息屏计时并唤醒
    void notifyActivity();
    void setState(const String& key, const String& message);
    void setNetworkStatus(bool wifiOk, bool mqttOk);
    void setDimmed(bool dimmed);
    void update();

private:
    void renderMid(uint32_t now, bool force);  // 中间状态块（背景+文字）
    void renderCharge(uint32_t now);           // running 充电式进度条
    void renderDots(uint32_t now);             // waiting 打字三点动画
    void renderFrame();                        // 上下白色边框
    void renderStatusIndicators();
    void refreshClock();
    uint16_t stateBg(float lum) const;         // 状态纯色块背景（支持呼吸亮度）
    uint16_t textFg() const;                   // 对比色文字（亮底黑字/暗底白字）
    bool isRunning() const;
    bool isWaiting() const;

    TFT_eSPI m_tft;
    bool m_dimmed = false;
    bool m_wifiOk = false;
    bool m_mqttOk = false;
    const StateDef* m_state = nullptr;
    uint32_t m_lastClockMs = 0;
    uint32_t m_lastAnimMs = 0;
    bool m_needFullRedraw = true;
    bool m_needChangedRedraw = false;
    int m_lastCells = -1;      // 充电格状进度条：已亮格数（增量绘制防闪）
    bool m_chargeReset = true; // 需要重绘全部格底
    uint32_t m_lastActivityMs = 0;  // 最后活动时间（息屏计时）
    bool m_screenOff = false;       // 息屏中
};

#endif // DISPLAY_PANEL_H