#include "DisplayPanel.h"
#include "ChineseFont.h"

// 配色
#define FRAME_COLOR  0xFFFF    // 上下边框：纯白
#define FRAME_TEXT   0x0000    // 边框内文字：黑
#define OFF_TEXT     0xCC0000  // 离线警示（白底上的深红）
#define WAVE_COLOR   0x0000    // 流水波：黑（黄底）

// 边框：上下各 12% 屏高（240*12% ≈ 29px）
static constexpr int FRAME_H = 29;
static constexpr int MID_Y = FRAME_H;                   // 中间区上界 29
static constexpr int MID_BOT = SCREEN_HEIGHT - FRAME_H; // 中间区下界 211
static constexpr int MID_H = MID_BOT - MID_Y;           // 182

// 中间区内容布局
static constexpr int TITLE_Y = 82;    // 大字 24px → 82..106
static constexpr int DESC_Y = 116;    // 描述 16px → 116..132
static constexpr int MSG_Y = 140;     // 消息 16px → 140..156
static constexpr int WAVE_Y = 44;     // 流水波带 → 44..66
static constexpr int WAVE_H = 22;

// CYD 板载 RGB LED：低电平点亮
static void ledWrite(uint8_t pin, bool on) {
    digitalWrite(pin, on ? LOW : HIGH);
}

// UTF-8 字符串的显示字符数（汉字按一字，ASCII 按一字符）
static int cnLen(const char* s) {
    int n = 0;
    while (*s) {
        if (((uint8_t)*s) < 0x80) { n++; s++; }
        else { n++; s += 3; }
    }
    return n;
}

// 标准纯色 565 编码（带亮度因子）
static uint16_t rgb565c(const RGBColor& c, float lum = 1.0f) {
    uint16_t r = (uint16_t)(c.r * lum);
    uint16_t g = (uint16_t)(c.g * lum);
    uint16_t b = (uint16_t)(c.b * lum);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void DisplayPanel::begin() {
    m_tft.init();
    m_tft.setRotation(SCREEN_ROTATION);
    m_tft.fillScreen(FRAME_COLOR);

    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_BLUE_PIN, OUTPUT);
    ledWrite(LED_GREEN_PIN, false);
    ledWrite(LED_BLUE_PIN, false);

    configTime(NTP_GMT_OFFSET_SEC, 0, NTP_SERVER1, NTP_SERVER2);
}

uint16_t DisplayPanel::stateBg(float lum) const {
    if (m_state == nullptr) return rgb565c(RGBColor(120, 120, 120), lum);
    return rgb565c(RGBColor(m_state->r, m_state->g, m_state->b), lum);
}

uint16_t DisplayPanel::textFg() const {
    if (m_dimmed) return FRAME_COLOR;  // 离线：统一白字
    if (m_state == nullptr) return FRAME_COLOR;
    int sum = m_state->r + m_state->g + m_state->b;
    return sum > 380 ? FRAME_TEXT : FRAME_COLOR;  // 亮底黑字，暗底白字
}

bool DisplayPanel::isRunning() const {
    return m_state != nullptr && strcmp(m_state->key, "running") == 0;
}

bool DisplayPanel::isWaiting() const {
    return m_state != nullptr && strcmp(m_state->key, "waiting") == 0;
}

void DisplayPanel::setState(const String& key, const String& message) {
    const StateDef* def = lookupState(key.c_str());
    if (def != m_state || m_message != message) {
        m_state = def;
        m_message = message;
        m_needChangedRedraw = true;
    }
}

void DisplayPanel::setNetworkStatus(bool wifiOk, bool mqttOk) {
    if (m_wifiOk != wifiOk || m_mqttOk != mqttOk) {
        m_wifiOk = wifiOk;
        m_mqttOk = mqttOk;
        renderStatusIndicators();
    }
}

void DisplayPanel::setDimmed(bool dimmed) {
    if (m_dimmed != dimmed) {
        m_dimmed = dimmed;
        m_needFullRedraw = true;
    }
}

// ====== 中间状态块（纯色背景 + 文字）；force=true 表示状态切换整块重绘 ======
void DisplayPanel::renderMid(uint32_t now, bool force) {
    if (m_state == nullptr) return;

    float lum = 1.0f;
    if (isWaiting()) {
        // 青色呼吸闪烁：整块亮度正弦变化
        lum = 0.35f + 0.65f * (0.5f + 0.5f * sinf(2.0f * PI * 1.2f * (now / 1000.0f)));
    }

    uint16_t bg = stateBg(m_dimmed ? lum * 0.2f : lum);
    m_tft.fillRect(0, MID_Y, SCREEN_WIDTH, MID_H, bg);
    uint16_t fg = textFg();

    // running 流水波（黄块上的黑色波线）
    if (isRunning() && force) {
        renderFlow(now);
    }

    // 状态大字 24px
    int bigW = 24 * cnLen(m_state->cnName);
    ChineseFont::draw(m_tft, (SCREEN_WIDTH - bigW) / 2, TITLE_Y, m_state->cnName, fg, bg, true, 8);

    // 描述 16px
    int descW = 16 * cnLen(m_state->cnDesc);
    ChineseFont::draw(m_tft, (SCREEN_WIDTH - descW) / 2, DESC_Y, m_state->cnDesc, fg, bg, false, 16);

    // 消息 16px（单行截断）
    if (m_message.length() > 0) {
        ChineseFont::draw(m_tft, 10, MSG_Y, m_message.c_str(), fg, bg, false, 17);
    }
}

// ====== running 流水波（局部刷新，避免整块重绘）======
void DisplayPanel::renderFlow(uint32_t now) {
    float t = now / 1000.0f;
    uint16_t bg = stateBg(m_dimmed ? 0.2f : 1.0f);
    m_tft.fillRect(10, WAVE_Y, SCREEN_WIDTH - 20, WAVE_H, bg);

    for (int i = 0; i < 2; i++) {
        float phase = i * 3.0f;
        int cy = WAVE_Y + WAVE_H / 2 + (i == 0 ? 0 : 3);
        for (int x = 0; x < SCREEN_WIDTH - 20; x += 3) {
            int y = cy + (int)(sinf(x * 0.08f + t * 5.0f + phase) * 6);
            m_tft.drawFastHLine(x + 10, y, 3, m_dimmed ? bg : WAVE_COLOR);
        }
    }
}

// ====== 上下白色边框 ======
void DisplayPanel::renderFrame() {
    m_tft.fillRect(0, 0, SCREEN_WIDTH, FRAME_H, FRAME_COLOR);
    m_tft.setTextFont(2);
    m_tft.setTextColor(FRAME_TEXT, FRAME_COLOR);
    m_tft.setTextDatum(TL_DATUM);
    m_tft.setCursor(10, 6);
    time_t now = time(nullptr);
    if (now > 1600000000) {
        struct tm* t = localtime(&now);
        if (t != nullptr) {
            char dateBuf[16];
            strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", t);
            m_tft.print(dateBuf);
        }
    } else {
        m_tft.print("-- -- --");
    }
    refreshClock();

    renderStatusIndicators();
}

void DisplayPanel::renderStatusIndicators() {
    m_tft.fillRect(0, MID_BOT, SCREEN_WIDTH, FRAME_H, FRAME_COLOR);
    m_tft.setTextFont(2);
    m_tft.setTextDatum(TL_DATUM);
    m_tft.setTextColor(FRAME_TEXT, FRAME_COLOR);

    int x = 12;
    m_tft.setCursor(x, SCREEN_HEIGHT - 22);
    m_tft.print("WiFi");
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 15, 3, m_wifiOk ? TFT_GREEN : TFT_RED);
    x += 54;

    m_tft.setCursor(x, SCREEN_HEIGHT - 22);
    m_tft.print("MQTT");
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 15, 3, m_mqttOk ? TFT_GREEN : TFT_RED);
    x += 62;

    m_tft.setCursor(x, SCREEN_HEIGHT - 22);
    m_tft.print("NTP");
    bool ntpOk = time(nullptr) > 1600000000;
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 15, 3, ntpOk ? TFT_GREEN : TFT_RED);

    if (m_dimmed) {
        m_tft.setTextColor(OFF_TEXT, FRAME_COLOR);
        m_tft.setTextDatum(TR_DATUM);
        m_tft.drawString("OFFLINE", SCREEN_WIDTH - 8, SCREEN_HEIGHT - 21);
        m_tft.setTextDatum(TL_DATUM);
    }
}

void DisplayPanel::refreshClock() {
    // 只刷新上边框右侧时间区域
    m_tft.fillRect(150, 2, SCREEN_WIDTH - 154, FRAME_H - 4, FRAME_COLOR);
    m_tft.setTextFont(4);
    m_tft.setTextDatum(TR_DATUM);
    m_tft.setTextColor(FRAME_TEXT, FRAME_COLOR);
    char buf[16];
    time_t now = time(nullptr);
    if (now > 1600000000) {
        struct tm* t = localtime(&now);
        if (t == nullptr) return;
        strftime(buf, sizeof(buf), "%H:%M:%S", t);
    } else {
        snprintf(buf, sizeof(buf), "UP %lum", (unsigned long)(millis() / 60000));
    }
    m_tft.drawString(buf, SCREEN_WIDTH - 8, 2);
    m_tft.setTextDatum(TL_DATUM);
}

void DisplayPanel::update() {
    uint32_t now = millis();

    if (m_needFullRedraw) {
        m_tft.fillScreen(FRAME_COLOR);
        renderFrame();
        renderMid(now, true);
        m_needFullRedraw = false;
        m_needChangedRedraw = false;
        m_lastAnimMs = now;
    } else if (m_needChangedRedraw) {
        renderMid(now, true);
        m_needChangedRedraw = false;
        m_lastAnimMs = now;
    }

    // 时钟：1 秒刷新
    if (now - m_lastClockMs >= 1000) {
        m_lastClockMs = now;
        refreshClock();
    }

    // 状态专属动效
    if (isRunning()) {
        if (now - m_lastAnimMs >= 33) {   // 流水 30fps
            m_lastAnimMs = now;
            renderFlow(now);
        }
    } else if (isWaiting()) {
        if (now - m_lastAnimMs >= 66) {   // 呼吸 15fps
            m_lastAnimMs = now;
            renderMid(now, false);
        }
    }

    // 板载 LED：在线=蓝常亮，有 WiFi 无 MQTT=绿蓝交替，离线=蓝快闪
    if (m_mqttOk) {
        ledWrite(LED_BLUE_PIN, true);
        ledWrite(LED_GREEN_PIN, false);
    } else if (m_wifiOk) {
        bool phase = (now / 500) % 2 == 0;
        ledWrite(LED_BLUE_PIN, phase);
        ledWrite(LED_GREEN_PIN, !phase);
    } else {
        bool phase = (now / 250) % 2 == 0;
        ledWrite(LED_BLUE_PIN, phase);
        ledWrite(LED_GREEN_PIN, false);
    }
}