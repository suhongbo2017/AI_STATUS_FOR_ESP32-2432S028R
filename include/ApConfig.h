#ifndef AP_CONFIG_H
#define AP_CONFIG_H

#include <Arduino.h>

// 阻塞式 AP 配网：软AP + DNS 捕获 + HTTP 配置页（192.168.4.1）
// 用户在网页提交 WiFi/MQTT 配置后，saveFn 被调用（由调用方保存并重启）。
// refreshFn 每 100ms 调用一次，用于驱动屏幕刷新与保活（配网期间不息屏）。
// 本函数不会返回（成功保存后由 saveFn 触发重启）。
void startApConfig(const String& ssidPrefix,
                   const char* defSsid, const char* defPass,
                   const char* defHost, const char* defPort,
                   void (*saveFn)(const String&, const String&,
                                  const String&, const String&),
                   void (*refreshFn)());

#endif // AP_CONFIG_H