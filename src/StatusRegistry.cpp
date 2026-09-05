#include "StatusRegistry.h"

// ====== 状态注册表（唯一数据源）======
// 覆盖来源：Pi 扩展（init/idle/running/done/error）+ 测试层（waiting/throttled/critical）
// 颜色：深色背景看板用明亮高辨识度色
static const StateDef STATUS_TABLE[] = {
    { "init",      "初始化",   "系统启动中", StatusClass::BUSY,  176,  91, 255 },  // 紫
    { "idle",      "空闲",     "等待任务",   StatusClass::READY,  46, 134, 255 },  // 蓝
    { "running",   "运行中",   "正在处理",   StatusClass::BUSY,  255, 200,  53 },  // 黄
    { "waiting",   "等待中",   "等待输入",   StatusClass::BUSY,   47, 224, 200 },  // 青
    { "throttled", "限流",     "限流冷却",   StatusClass::BUSY,  255, 154,  46 },  // 橙
    { "done",      "已完成",   "任务完成",   StatusClass::READY,  46, 204, 113 },  // 绿
    { "error",     "出错",     "任务出错",   StatusClass::ERROR, 255,  77,  77 },  // 红
    { "critical",  "严重故障", "请检查连接", StatusClass::ERROR, 255,  46, 136 },  // 品红
};

// 未命中时的兜底条目（灰色）
static const StateDef UNKNOWN_STATE = {
    "unknown", "未知状态", "未知状态", StatusClass::UNKNOWN, 138, 147, 160
};

const StateDef* lookupState(const char* key) {
    if (key != nullptr) {
        for (const auto& s : STATUS_TABLE) {
            if (strcmp(key, s.key) == 0) return &s;
        }
    }
    return &UNKNOWN_STATE;
}

const char* classCnName(StatusClass cls) {
    switch (cls) {
        case StatusClass::READY:   return "就绪";
        case StatusClass::BUSY:    return "进行中";
        case StatusClass::ERROR:   return "异常";
        default:                   return "未知";
    }
}