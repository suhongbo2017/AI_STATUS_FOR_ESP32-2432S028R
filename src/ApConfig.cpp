#include "ApConfig.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

static WebServer server(80);
static DNSServer dns;

static void (*gSaveFn)(const String&, const String&, const String&, const String&) = nullptr;
static void (*gRefreshFn)() = nullptr;
static String gDefSsid, gDefPass, gDefHost, gDefPort;

// 配置页（占位符 __SSID__/__PASS__/__HOST__/__PORT__ 提交前替换为默认值）
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI 状态看板 · 网络配置</title><style>
body{font-family:system-ui,sans-serif;max-width:460px;margin:32px auto;padding:0 18px;color:#222;background:#f7f8fa}
h2{font-size:20px;margin:0 0 4px}p{color:#666;font-size:13px;margin:0 0 18px}
label{display:block;margin:12px 0 4px;font-size:13px;color:#444}
input{width:100%;box-sizing:border-box;padding:10px;border:1px solid #ccc;border-radius:6px;font-size:15px}
button{width:100%;margin-top:20px;padding:13px;background:#0787e0;color:#fff;border:0;border-radius:6px;font-size:16px}
.foot{margin-top:18px;font-size:12px;color:#999;text-align:center}
</style></head><body>
<h2>AI 状态看板 · 网络配置</h2>
<p>连接本热点后，填写家里/办公室 WiFi 信息与 MQTT 服务器，保存后设备自动重启联网。</p>
<form method="POST" action="/save">
<label>WiFi 名称 (SSID)</label>
<input name="ssid" value="__SSID__" required>
<label>WiFi 密码</label>
<input name="pass" type="password" value="__PASS__">
<label>MQTT 服务器地址</label>
<input name="host" value="__HOST__" required>
<label>MQTT 端口</label>
<input name="port" value="__PORT__">
<button type="submit">保存并重启</button>
</form>
<div class="foot">AI_STATUS_FOR_ESP32-2432S028R</div>
</body></html>
)rawliteral";

static void handleRoot() {
    String html = FPSTR(PAGE_HTML);
    html.replace("__SSID__", gDefSsid);
    html.replace("__PASS__", gDefPass);
    html.replace("__HOST__", gDefHost);
    html.replace("__PORT__", gDefPort);
    server.send(200, "text/html; charset=utf-8", html);
}

static void handleSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String host = server.arg("host");
    String port = server.arg("port");
    if (ssid.length() == 0 || host.length() == 0) {
        server.send(400, "text/html; charset=utf-8", "<h3>参数缺失：SSID 与 MQTT 服务器必填</h3>"
                     "<a href='/'>返回</a>");
        return;
    }
    server.send(200, "text/html; charset=utf-8",
                "<h3>已保存，设备重启中…</h3><p>请等待设备重新连接 WiFi</p>");
    Serial.printf("[配网] 收到配置: ssid=%s host=%s:%s\n", ssid.c_str(), host.c_str(), port.c_str());
    if (gSaveFn) gSaveFn(ssid, pass, host, port);
}

void startApConfig(const String& ssidPrefix,
                   const char* defSsid, const char* defPass,
                   const char* defHost, const char* defPort,
                   void (*saveFn)(const String&, const String&,
                                  const String&, const String&),
                   void (*refreshFn)()) {
    gSaveFn = saveFn;
    gRefreshFn = refreshFn;
    gDefSsid = defSsid;
    gDefPass = defPass;
    gDefHost = defHost;
    gDefPort = defPort;

    // 热点名：前缀 + MAC 尾 4 位（区分多台设备）
    char apName[32];
    uint32_t mac = (uint32_t)(ESP.getEfuseMac() >> 24) & 0xFFFF;
    snprintf(apName, sizeof(apName), "%s-%04X", ssidPrefix.c_str(), mac);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName);
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[配网] 热点已启动: %s | 配置地址: http://%s\n", apName, apIP.toString().c_str());

    dns.start(53, "*", apIP);   // 任意域名都转到配置页
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();

    // 阻塞服务，直到 /save 触发保存并重启
    unsigned long last = 0;
    while (true) {
        dns.processNextRequest();
        server.handleClient();
        if (gRefreshFn && millis() - last >= 100) {
            last = millis();
            gRefreshFn();
        }
        delay(5);
    }
}