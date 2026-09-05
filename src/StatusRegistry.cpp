#include "StatusRegistry.h"

// ====== 5 个核心状态（标准纯色）======
static const StateDef STATUS_TABLE[] = {
    { "idle",    "等待", "等待任务",   0,   0, 255 },  // 蓝
    { "running", "运行", "正在处理", 255, 255,   0 },  // 黄
    { "waiting", "输入", "等待输入",   0, 255, 255 },  // 青
    { "done",    "完成", "任务完成",   0, 255,   0 },  // 绿
    { "error",   "出错", "任务出错", 255,   0,   0 },  // 红
};

// 旧状态合并到核心状态的别名表
static const struct Alias { const char* from; const char* to; } ALIAS_TABLE[] = {
    { "init",      "idle" },     // 初始化 → 等待
    { "throttled", "idle" },     // 限流冷却 → 等待
    { "cmd",       "running" },  // 执行命令 → 运行
    { "critical",  "error" },    // 严重故障 → 出错
};

// 未命中时的兜底条目（灰色）
static const StateDef UNKNOWN_STATE = {
    "unknown", "未知", "未知状态", 120, 120, 120
};

const StateDef* lookupState(const char* key) {
    if (key != nullptr) {
        for (const auto& s : STATUS_TABLE) {
            if (strcmp(key, s.key) == 0) return &s;
        }
        for (const auto& a : ALIAS_TABLE) {
            if (strcmp(key, a.from) == 0) {
                for (const auto& s : STATUS_TABLE) {
                    if (strcmp(a.to, s.key) == 0) return &s;
                }
            }
        }
    }
    return &UNKNOWN_STATE;
}