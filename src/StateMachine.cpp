#include "StateMachine.h"

const RGBColor RGBColor::Red     = RGBColor(255, 0, 0);
const RGBColor RGBColor::Green   = RGBColor(0, 255, 0);
const RGBColor RGBColor::Blue    = RGBColor(0, 0, 255);
const RGBColor RGBColor::Yellow  = RGBColor(255, 255, 0);
const RGBColor RGBColor::Cyan    = RGBColor(0, 255, 180); // 纯青色（减少蓝色分量）
const RGBColor RGBColor::Magenta = RGBColor(255, 0, 255);
const RGBColor RGBColor::Purple  = RGBColor(255, 0, 255);
const RGBColor RGBColor::Orange  = RGBColor(255, 165, 0);
const RGBColor RGBColor::White   = RGBColor(255, 255, 255);
const RGBColor RGBColor::Black   = RGBColor(0, 0, 0);

StateMachine::StateMachine()
    : m_rawState(WorkflowState::INIT)
    , m_brightnessMultiplier(255)
{
}

void StateMachine::setState(WorkflowState state) {
    // INIT 是启动态，始终可以被任何状态覆盖
    // CRITICAL 只能由外部消息强制覆盖（通过 forceSetState）
    // 其他状态：高优先级（数值小）覆盖低优先级，同状态允许刷新
    if (m_rawState == WorkflowState::INIT || isHigherPriority(state, m_rawState) || state == m_rawState) {
        m_rawState = state;
    }
    // 低优先级状态不覆盖高优先级状态
}

void StateMachine::forceSetState(WorkflowState state) {
    // 强制设置，忽略优先级（用于 MQTT 消息覆盖 CRITICAL 等）
    m_rawState = state;
}

WorkflowState StateMachine::getEffectiveState() const {
    return m_rawState;
}

LEDEffect StateMachine::getCurrentEffect() const {
    LEDEffect effect = getEffectForState(m_rawState);
    effect.brightness = (effect.brightness * m_brightnessMultiplier) / 255;
    return effect;
}

void StateMachine::setBrightnessMultiplier(uint8_t multiplier) {
    m_brightnessMultiplier = multiplier;
}

uint8_t StateMachine::getBrightnessMultiplier() const {
    return m_brightnessMultiplier;
}

void StateMachine::update() {
    // 预留
}

void StateMachine::printState(Stream& stream) const {
    stream.print("[StateMachine] State: ");
    stream.print(stateToString(m_rawState));
    stream.print(" | Brightness: ");
    stream.println(m_brightnessMultiplier);
}

const char* StateMachine::stateToString(WorkflowState state) {
    switch (state) {
        case WorkflowState::CRITICAL:  return "critical";
        case WorkflowState::ERROR:     return "error";
        case WorkflowState::RUNNING:   return "running";
        case WorkflowState::WAITING:   return "waiting";
        case WorkflowState::THROTTLED: return "throttled";
        case WorkflowState::DONE:      return "done";
        case WorkflowState::INIT:      return "init";
        case WorkflowState::IDLE:      return "idle";
        default:                       return "unknown";
    }
}

WorkflowState StateMachine::stringToState(const String& str) {
    if (str == "critical")  return WorkflowState::CRITICAL;
    if (str == "error")     return WorkflowState::ERROR;
    if (str == "running")   return WorkflowState::RUNNING;
    if (str == "waiting")   return WorkflowState::WAITING;
    if (str == "throttled") return WorkflowState::THROTTLED;
    if (str == "done")      return WorkflowState::DONE;
    if (str == "init")      return WorkflowState::INIT;
    if (str == "idle")      return WorkflowState::IDLE;
    return WorkflowState::IDLE; // 默认空闲
}

RGBColor StateMachine::colorNameToRGB(const String& name) {
    if (name == "red")      return RGBColor::Red;
    if (name == "green")    return RGBColor::Green;
    if (name == "blue")     return RGBColor::Blue;
    if (name == "yellow")   return RGBColor::Yellow;
    if (name == "cyan")     return RGBColor::Cyan;
    if (name == "magenta")  return RGBColor::Magenta;
    if (name == "purple")   return RGBColor::Purple;
    if (name == "orange")   return RGBColor::Orange;
    if (name == "white")    return RGBColor::White;
    if (name == "black")    return RGBColor::Black;
    return RGBColor::Blue; // 默认蓝色
}

LEDEffect StateMachine::getEffectForState(WorkflowState state) {
    LEDEffect effect;
    effect.brightness = 255; // 默认全亮

    switch (state) {
        case WorkflowState::CRITICAL:
            effect.type = EffectType::ALTERNATE;
            effect.color1 = RGBColor::Red;
            effect.color2 = RGBColor::Blue;
            effect.periodMs = 500;
            break;

        case WorkflowState::ERROR:
            effect.type = EffectType::SOLID;
            effect.color1 = RGBColor::Red;
            break;

        case WorkflowState::RUNNING:
            effect.type = EffectType::CHASE;
            effect.color1 = RGBColor::Yellow;
            effect.color2 = RGBColor(64, 64, 0);  // 暗黄尾迹（屏幕以扫光表现）
            effect.periodMs = 3600;               // 完整扫光一轮约 3.6 秒
            break;

        case WorkflowState::WAITING:
            effect.type = EffectType::BLINK;
            effect.color1 = RGBColor::Cyan;
            effect.periodMs = 300;
            break;

        case WorkflowState::THROTTLED:
            effect.type = EffectType::BLINK;
            effect.color1 = RGBColor::Orange;
            effect.periodMs = 800;
            break;

        case WorkflowState::DONE:
            effect.type = EffectType::SOLID;
            effect.color1 = RGBColor::Green;
            break;

        case WorkflowState::INIT:
            effect.type = EffectType::BREATH;
            effect.color1 = RGBColor::Purple;
            effect.periodMs = 2000;
            break;

        case WorkflowState::IDLE:
            effect.type = EffectType::SOLID;
            effect.color1 = RGBColor::Blue;
            break;
    }

    return effect;
}