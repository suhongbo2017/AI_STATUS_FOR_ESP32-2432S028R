#ifndef STATUS_REGISTRY_H
#define STATUS_REGISTRY_H

#include <Arduino.h>

// RGB 颜色（0-255 分量）
struct RGBColor {
    uint8_t r, g, b;

    RGBColor() : r(0), g(0), b(0) {}
    RGBColor(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
};

// 单个状态定义（看板只保留 5 个核心状态，其余通过别名合并，见 ALIAS_TABLE）
struct StateDef {
    const char* key;      // MQTT 状态名（小写）
    const char* cnName;   // 中文名（大字）
    const char* cnDesc;   // 中文描述
    uint8_t r, g, b;      // 标准纯色
};

// 按 MQTT key 查找状态；先查 5 状态表，再查别名合并，最后 UNKNOWN 灰色兜底
const StateDef* lookupState(const char* key);

#endif // STATUS_REGISTRY_H