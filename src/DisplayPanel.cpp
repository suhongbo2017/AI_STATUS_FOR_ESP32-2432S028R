#include "DisplayPanel.h"

// 看板配色（深色仪表盘风格）
#define BG_COLOR      0x0E1116  // 内容区背景（深蓝黑）
#define BAR_COLOR     0x1A2029  // 顶栏/底栏背景
#define BAR_TEXT      0xC8D0DC  // 栏内文字灰白
#define BAR_TEXT_DIM  0x5A6472  // 栏内弱化文字

static constexpr int TOP_BAR_H = 26;
static constexpr int BOT_BAR_H = 22;
static constexpr int BADGE_X = 16;
static constexpr int BADGE_Y = 52;
static constexpr int BADGE_S = 140;
static constexpr int PROG_Y = 200;
static constexpr int PROG_H = 8;

void DisplayPanel::begin() {
    m_tft.init();
    m_tft.setRotation(SCREEN_ROTATION);
    m_tft.fillScreen(BG_COLOR);

    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_BLUE_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, HIGH);  // 低电平点亮，默认熄灭
    digitalWrite(LED_BLUE_PIN, HIGH);

    configTime(NTP_GMT_OFFSET_SEC, 0, NTP_SERVER1, NTP_SERVER2);
}

uint16_t DisplayPanel::dim(uint16_t color565) const {
    if (!m_dimmed) return color565;
    uint16_t r = (uint16_t)(((color565 >> 11) & 0x1F) * 0.2f);
    uint16_t g = (uint16_t)(((color565 >> 5) & 0x3F) * 0.2f);
    uint16_t b = (uint16_t)((color565 & 0x1F) * 0.2f);
    return (r << 11) | (g << 5) | b;
}

uint16_t DisplayPanel::rgb565(const RGBColor& c) {
    return m_tft.color565(c.r, c.g, c.b);
}

String DisplayPanel::badgeText() const {
    String s = m_stateName;
    s.toUpperCase();
    if (s == "INIT")      return "INIT";
    if (s == "IDLE")      return "OK";
    if (s == "RUNNING")   return "RUN";
    if (s == "DONE")      return "DONE";
    if (s == "ERROR")     return "ERR";
    if (s == "WAITING")   return "WAIT";
    if (s == "THROTTLED") return "SLOW";
    if (s == "CRITICAL")  return "CRIT";
    if (s == "CMD")       return "CMD";
    if (s.length() > 4)   return s.substring(0, 4);
    return s;
}

const char* DisplayPanel::stateDescription() const {
    String s = m_stateName;
    s.toUpperCase();
    if (s == "INIT")      return "System starting...";
    if (s == "IDLE")      return "Ready for tasks";
    if (s == "RUNNING")   return "AI is working";
    if (s == "DONE")      return "Task completed";
    if (s == "ERROR")     return "Task failed";
    if (s == "WAITING")   return "Waiting for input";
    if (s == "THROTTLED") return "Cooling down";
    if (s == "CRITICAL")  return "Critical failure";
    if (s == "CMD")       return "Manual control";
    return "";
}

bool DisplayPanel::isMessagePrintable() const {
    for (unsigned int i = 0; i < m_message.length(); i++) {
        char c = m_message[i];
        if (c < 0x20 || c > 0x7E) return false;  // 含非 ASCII（无中文字库）
    }
    return true;
}

void DisplayPanel::setEffect(const LEDEffect& effect) {
    m_statusColor = effect.color1;
    m_needFullRedraw = true;
}

void DisplayPanel::setStateName(const String& name) {
    String upper = name;
    upper.toUpperCase();
    if (m_stateName != upper) {
        m_stateName = upper;
        m_needFullRedraw = true;
    }
}

void DisplayPanel::setMessage(const String& message) {
    m_message = message;
    m_needFullRedraw = true;
}

void DisplayPanel::setNetworkStatus(bool wifiOk, bool mqttOk) {
    if (m_wifiOk != wifiOk || m_mqttOk != mqttOk) {
        m_wifiOk = wifiOk;
        m_mqttOk = mqttOk;
        renderStatusBar();
    }
}

void DisplayPanel::setDimmed(bool dimmed) {
    if (m_dimmed != dimmed) {
        m_dimmed = dimmed;
        m_needFullRedraw = true;
    }
}

// ====== 整屏看板渲染 ======
void DisplayPanel::renderDashboard() {
    m_tft.fillScreen(dim(BG_COLOR));
    renderTopBar();
    renderBadge();
    renderRightPanel();
    renderStatusBar();
    m_needFullRedraw = false;
}

void DisplayPanel::renderTopBar() {
    m_tft.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_H, BAR_COLOR);
    m_tft.setTextDatum(TL_DATUM);
    m_tft.setTextFont(2);
    m_tft.setTextColor(BAR_TEXT, BAR_COLOR);
    m_tft.setCursor(10, 8);
    m_tft.print("AI WORKFLOW STATUS");
    // 状态色小圆点（顶栏右侧，时钟左侧）
    m_tft.fillCircle(232, 13, 5, rgb565(m_statusColor));
    refreshClock();
}

void DisplayPanel::renderBadge() {
    uint16_t color = dim(rgb565(m_statusColor));
    m_tft.fillRoundRect(BADGE_X, BADGE_Y, BADGE_S, BADGE_S, 18, color);
    // 徽章内描边，增强层次
    m_tft.drawRoundRect(BADGE_X + 6, BADGE_Y + 6, BADGE_S - 12, BADGE_S - 12, 14,
                        rgb565(RGBColor(255, 255, 255)) & 0x7FFF);  // 半透明白描边
    // 徽章中央状态缩写
    m_tft.setTextDatum(MC_DATUM);
    m_tft.setTextFont(4);
    m_tft.setTextColor(TFT_WHITE, color);
    m_tft.drawString(badgeText(), BADGE_X + BADGE_S / 2, BADGE_Y + BADGE_S / 2);
    m_tft.setTextDatum(TL_DATUM);
}

void DisplayPanel::renderRightPanel() {
    int rx = BADGE_X + BADGE_S + 18;   // 右侧起点
    int rw = SCREEN_WIDTH - rx - 6;    // 右侧宽度

    // 状态名（大字）
    m_tft.setTextDatum(MC_DATUM);
    m_tft.setFreeFont(&FreeSansBold18pt7b);
    m_tft.setTextColor(dim(TFT_WHITE), dim(BG_COLOR));
    m_tft.drawString(m_stateName, rx + rw / 2, 86);

    // 状态描述（小字浅灰）
    m_tft.setFreeFont(&FreeSans12pt7b);
    m_tft.setTextColor(dim(BAR_TEXT_DIM), dim(BG_COLOR));
    m_tft.drawString(stateDescription(), rx + rw / 2, 120);

    // 消息（仅可打印 ASCII 显示；含中文等显示提示）
    m_tft.setTextFont(2);
    if (m_message.length() > 0) {
        String disp = isMessagePrintable() ? m_message : "MSG";
        if (disp.length() > 24) disp = disp.substring(0, 23) + "...";
        m_tft.setTextColor(dim(BAR_TEXT), dim(BG_COLOR));
        m_tft.drawString(disp, rx + rw / 2, 152);
    }
    m_tft.setTextDatum(TL_DATUM);
}

void DisplayPanel::renderProgressBar(uint32_t now) {
    if (m_stateName != "RUNNING") {
        // 清掉旧进度条区域（画背景色）
        m_tft.fillRect(16, PROG_Y, SCREEN_WIDTH - 32, PROG_H, dim(BG_COLOR));
        return;
    }
    const uint32_t cycleMs = 2000;
    uint16_t fillW = (uint16_t)((float)(now % cycleMs) / cycleMs * (SCREEN_WIDTH - 32));
    uint16_t color = dim(rgb565(m_statusColor));
    m_tft.drawRoundRect(16, PROG_Y, SCREEN_WIDTH - 32, PROG_H, PROG_H / 2, dim(BAR_TEXT_DIM));
    if (fillW > 0) {
        m_tft.fillRoundRect(16, PROG_Y, fillW, PROG_H, PROG_H / 2, color);
    }
}

void DisplayPanel::renderStatusBar() {
    m_tft.fillRect(0, SCREEN_HEIGHT - BOT_BAR_H, SCREEN_WIDTH, BOT_BAR_H, BAR_COLOR);
    m_tft.setTextFont(2);
    m_tft.setTextDatum(TL_DATUM);
    m_tft.setTextColor(BAR_TEXT, BAR_COLOR);

    int x = 10;
    m_tft.setCursor(x, SCREEN_HEIGHT - 16);
    m_tft.print("WiFi");
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 9, 3, m_wifiOk ? TFT_GREEN : TFT_RED);
    x += 54;

    m_tft.setCursor(x, SCREEN_HEIGHT - 16);
    m_tft.print("MQTT");
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 9, 3, m_mqttOk ? TFT_GREEN : TFT_RED);
    x += 62;

    m_tft.setCursor(x, SCREEN_HEIGHT - 16);
    m_tft.print("NTP");
    bool ntpOk = time(nullptr) > 1600000000;
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 9, 3, ntpOk ? TFT_GREEN : TFT_RED);

    // 离线提示（右侧）
    if (m_dimmed) {
        m_tft.setTextColor(TFT_RED, BAR_COLOR);
        m_tft.setTextDatum(TR_DATUM);
        m_tft.drawString("OFFLINE", SCREEN_WIDTH - 8, SCREEN_HEIGHT - 15);
        m_tft.setTextDatum(TL_DATUM);
    }
}

void DisplayPanel::refreshClock() {
    char buf[16];
    time_t now = time(nullptr);
    if (now > 1600000000) {
        struct tm* t = localtime(&now);
        if (t == nullptr) return;
        strftime(buf, sizeof(buf), "%H:%M:%S", t);
    } else {
        snprintf(buf, sizeof(buf), "UP %lum", (unsigned long)(millis() / 60000));
    }
    m_tft.fillRect(240, 4, SCREEN_WIDTH - 244, 20, BAR_COLOR);
    m_tft.setTextFont(2);
    m_tft.setTextColor(BAR_TEXT, BAR_COLOR);
    m_tft.setTextDatum(TR_DATUM);
    m_tft.drawString(buf, SCREEN_WIDTH - 8, 7);
    m_tft.setTextDatum(TL_DATUM);
}

void DisplayPanel::update() {
    uint32_t now = millis();

    // 全屏重绘（状态/消息/变暗变化时）
    if (m_needFullRedraw) {
        renderDashboard();
    }

    // 时钟：1 秒刷新
    if (now - m_lastClockMs >= 1000) {
        m_lastClockMs = now;
        refreshClock();
    }

    // running 进度条动画：30fps
    if (m_stateName == "RUNNING") {
        if (now - m_lastFrameMs >= 33) {
            m_lastFrameMs = now;
            renderProgressBar(now);
        }
    }

    // 板载 LED：在线=蓝常亮，有 WiFi 无 MQTT=绿蓝交替，离线=蓝快闪
    if (m_mqttOk) {
        digitalWrite(LED_BLUE_PIN, LOW);
        digitalWrite(LED_GREEN_PIN, HIGH);
    } else if (m_wifiOk) {
        bool phase = (now / 500) % 2 == 0;
        digitalWrite(LED_BLUE_PIN, phase ? LOW : HIGH);
        digitalWrite(LED_GREEN_PIN, phase ? HIGH : LOW);
    } else {
        bool phase = (now / 250) % 2 == 0;
        digitalWrite(LED_BLUE_PIN, phase ? LOW : HIGH);
        digitalWrite(LED_GREEN_PIN, HIGH);
    }
}