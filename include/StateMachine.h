#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>

// 8 种工作流状态（按优先级从高到低排列）
enum class WorkflowState : uint8_t {
    CRITICAL  = 0,  // 严重故障，红蓝交替
    ERROR     = 1,  // 出错/停止，红色常亮
    RUNNING   = 2,  // 运行中，黄色闪烁
    WAITING   = 3,  // 等待输入，青色闪烁
    THROTTLED = 4,  // 限流/冷却，橙色慢闪
    DONE      = 5,  // 完成，绿色常亮
    INIT      = 6,  // 初始化，紫色呼吸
    IDLE      = 7   // 空闲，蓝色常亮
};

// 优先级比较：数值越小优先级越高
inline bool isHigherPriority(WorkflowState a, WorkflowState b) {
    return static_cast<uint8_t>(a) < static_cast<uint8_t>(b);
}

// RGB 颜色（替代 FastLED 的 CRGB）
struct RGBColor {
    uint8_t r, g, b;

    RGBColor() : r(0), g(0), b(0) {}
    RGBColor(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}

    static const RGBColor Red;
    static const RGBColor Green;
    static const RGBColor Blue;
    static const RGBColor Yellow;
    static const RGBColor Cyan;
    static const RGBColor Magenta;
    static const RGBColor Purple;
    static const RGBColor Orange;
    static const RGBColor White;
    static const RGBColor Black;
};

// 灯光效果类型（屏幕看板也沿用同一套语义）
enum class EffectType : uint8_t {
    SOLID,      // 常亮
    BLINK,      // 闪烁（可配置周期）
    BREATH,     // 呼吸（正弦波渐变）
    ALTERNATE,  // 交替（两种颜色交替）
    CHASE       // 扫光（移动高亮带）
};

// 灯光效果描述
struct LEDEffect {
    EffectType type;
    RGBColor color1;        // 主色
    RGBColor color2;        // 副色（仅 ALTERNATE/CHASE 使用）
    uint16_t periodMs;      // 周期毫秒
    uint8_t brightness;     // 亮度 0-255

    LEDEffect()
        : type(EffectType::SOLID), color1(RGBColor::Black), color2(RGBColor::Black),
          periodMs(1000), brightness(255) {}
};

// 状态机类
class StateMachine {
public:
    StateMachine();

    // 设置当前状态（高优先级状态覆盖低优先级）
    void setState(WorkflowState state);

    // 强制设置状态（忽略优先级，用于 MQTT 消息强制覆盖）
    void forceSetState(WorkflowState state);

    // 获取当前有效状态
    WorkflowState getEffectiveState() const;

    // 获取当前状态的灯光效果
    LEDEffect getCurrentEffect() const;

    // 设置全局亮度倍率（0-255，用于离线降级）
    void setBrightnessMultiplier(uint8_t multiplier);

    // 获取全局亮度倍率
    uint8_t getBrightnessMultiplier() const;

    // 更新状态机（每帧调用）
    void update();

    // 调试输出当前状态
    void printState(Stream& stream = Serial) const;

    // 状态名转字符串
    static const char* stateToString(WorkflowState state);

    // 字符串转状态（从 MQTT 消息解析）
    static WorkflowState stringToState(const String& str);

    // 颜色名转 RGB（用于 ai/led/command 直接控制）
    static RGBColor colorNameToRGB(const String& name);

    // 获取最近一次 setState 的值（不考虑优先级）
    WorkflowState getRawState() const { return m_rawState; }

private:
    static LEDEffect getEffectForState(WorkflowState state);

    WorkflowState m_rawState;       // 最近一次设置的状态
    uint8_t m_brightnessMultiplier; // 亮度倍率 0-255
};

#endif // STATE_MACHINE_H