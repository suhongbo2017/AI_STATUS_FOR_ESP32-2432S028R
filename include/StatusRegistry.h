#ifndef STATUS_REGISTRY_H
#define STATUS_REGISTRY_H

#include <Arduino.h>

// RGB 颜色（0-255 分量）
struct RGBColor {
    uint8_t r, g, b;

    RGBColor() : r(0), g(0), b(0) {}
    RGBColor(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
};

// 状态分类：新增分类时在这里加一个枚举值 + classCnName 对应词
enum class StatusClass : uint8_t {
    READY,   // 就绪/正常：空闲、完成
    BUSY,    // 进行中：初始化、运行、等待、限流
    ERROR,   // 异常：出错、严重故障
    UNKNOWN  // 未知状态兜底
};

// 单个状态定义。新增状态 = 向 STATUS_TABLE 追加一行。
struct StateDef {
    const char* key;      // MQTT 状态名（小写）
    const char* cnName;   // 中文名
    const char* cnDesc;   // 中文描述
    StatusClass cls;      // 分类
    uint8_t r, g, b;      // 状态色（RGB 0-255）
};

// 按 MQTT key 查找状态；未命中返回 UNKNOWN 占位条目（显示原始 key）
const StateDef* lookupState(const char* key);

// 分类中文名
const char* classCnName(StatusClass cls);

#endif // STATUS_REGISTRY_H