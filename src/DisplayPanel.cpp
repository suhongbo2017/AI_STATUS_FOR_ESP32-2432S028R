#include "DisplayPanel.h"
#include "ChineseFont.h"

// 配色
#define FRAME_COLOR  0xB8902A  // 棕黄边框
#define FRAME_TEXT   0x000000  // 边框内文字（黑）
#define OFF_TEXT     0xE74C3C  // 离线警示红
#define BG_BLACK     0x000000

// 边框：上下各 12% 屏高（240*12% ≈ 29px）
static constexpr int FRAME_H = 29;
static constexpr int MID_Y = FRAME_H;                  // 29
static constexpr int MID_H = SCREEN_HEIGHT - FRAME_H * 2;  // 182
static constexpr int FLOW_Y = MID_Y + 8;               // 37
static constexpr int FLOW_H = 30;
static constexpr int TEX_X = 10;
static constexpr int TEX_Y = 76;
static constexpr int TEX_W = SCREEN_WIDTH - 20;        // 300
static constexpr int TEX_H = 96;
static constexpr int RIP_X = 85;
static constexpr int RIP_Y = 21;
static constexpr int RIP_S = 150;
static constexpr int PROG_Y = MID_Y + MID_H - 18;      // 193

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

void DisplayPanel::begin() {
    m_tft.init();
    m_tft.setRotation(SCREEN_ROTATION);
    m_tft.fillScreen(BG_BLACK);

    m_text.createSprite(TEX_W, TEX_H);
    m_text.setSwapBytes(true);
    m_flow.createSprite(SCREEN_WIDTH - 20, FLOW_H);
    m_flow.setSwapBytes(true);
    m_ripple.createSprite(RIP_S, RIP_S);
    m_ripple.setSwapBytes(true);

    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_BLUE_PIN, OUTPUT);
    ledWrite(LED_GREEN_PIN, false);
    ledWrite(LED_BLUE_PIN, false);

    configTime(NTP_GMT_OFFSET_SEC, 0, NTP_SERVER1, NTP_SERVER2);
}

uint16_t DisplayPanel::dim(uint16_t color565) const {
    if (!m_dimmed) return color565;
    uint16_t r = (uint16_t)(((color565 >> 11) & 0x1F) * 0.2f);
    uint16_t g = (uint16_t)(((color565 >> 5) & 0x3F) * 0.2f);
    uint16_t b = (uint16_t)((color565 & 0x1F) * 0.2f);
    return (r << 11) | (g << 5) | b;
}

uint16_t DisplayPanel::rgb565(const RGBColor& c) const {
    return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

uint16_t DisplayPanel::rgb565l(const RGBColor& c, float lum) const {
    float g = lum * (m_dimmed ? 0.2f : 1.0f);
    uint16_t r = (uint16_t)(c.r * g);
    uint16_t gg = (uint16_t)(c.g * g);
    uint16_t b = (uint16_t)(c.b * g);
    if (r > 255) r = 255;
    if (gg > 255) gg = 255;
    if (b > 255) b = 255;
    return rgb565(RGBColor((uint8_t)r, (uint8_t)gg, (uint8_t)b));
}

bool DisplayPanel::isRunning() const {
    return m_state != nullptr && strcmp(m_state->key, "running") == 0;
}

bool DisplayPanel::isWaiting() const {
    return m_state != nullptr && strcmp(m_state->key, "waiting") == 0;
}

bool DisplayPanel::isCmd() const {
    return m_state != nullptr && strcmp(m_state->key, "cmd") == 0;
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

// ====== 中间文字区（大字/描述/消息，waiting 时呼吸闪烁）======
void DisplayPanel::renderText(uint32_t now) {
    if (m_state == nullptr) return;
    float lum = 1.0f;
    if (isWaiting()) {
        lum = 0.35f + 0.65f * (0.5f + 0.5f * sinf(2.0f * PI * 1.2f * (now / 1000.0f)));
    }

    RGBColor col(m_state->r, m_state->g, m_state->b);
    m_text.fillSprite(BG_BLACK);

    // 状态大字 24px（状态色）
    int bigW = 24 * cnLen(m_state->cnName);
    ChineseFont::draw(m_text, (TEX_W - bigW) / 2, 8, m_state->cnName, rgb565l(col, lum), true, 8);

    // 描述 16px（白色弱化）
    int descW = 16 * cnLen(m_state->cnDesc);
    ChineseFont::draw(m_text, (TEX_W - descW) / 2, 44, m_state->cnDesc, dim(0x8A93A0), false, 16);

    // 消息 16px（白色）
    if (m_message.length() > 0) {
        ChineseFont::draw(m_text, 8, 72, m_message.c_str(), dim(0xC8D0DC), false, 17);
    }

    m_text.pushSprite(TEX_X, TEX_Y);
}

// ====== running 流水动画 ======
void DisplayPanel::renderFlow(uint32_t now) {
    m_flow.fillSprite(BG_BLACK);
    float t = now / 1000.0f;
    const int w = SCREEN_WIDTH - 20;
    const int mid = FLOW_H / 2;  // 15

    // 三条正弦波亮度递减，相位错开，随时间流动
    for (int i = 0; i < 3; i++) {
        float phase = i * 2.1f;
        uint8_t lum = (uint8_t)(255 - i * 80);
        RGBColor c(255, 200, 53);
        int cy = mid + (i - 1) * 7;
        for (int x = 0; x < w; x += 2) {
            float y = cy + sinf(x * 0.06f + t * 4.0f + phase) * 8;
            int intY = (int)y;
            m_flow.drawFastHLine(x, intY, 3, rgb565l(c, lum / 255.0f));
        }
    }
    m_flow.pushSprite(10, FLOW_Y);
}

// ====== cmd 涟漪动画（橙红圆环扩散，黑色透明底）======
void DisplayPanel::renderRipple(uint32_t now) {
    m_ripple.fillSprite(BG_BLACK);
    float t = now / 1000.0f;
    RGBColor c(255, 95, 42);
    const int cx = RIP_S / 2, cy = RIP_S / 2;

    // 三个错开相位的扩散环
    for (int i = 0; i < 3; i++) {
        float ph = fmodf(t * 1.8f + i * 0.33f, 1.0f);
        int r = 14 + (int)(ph * 62);
        float lum = 0.9f * (1.0f - ph);
        m_ripple.drawCircle(cx, cy, r, rgb565l(c, lum * 0.9f));
        if (r > 2) m_ripple.drawCircle(cx, cy, r - 2, rgb565l(c, lum * 0.5f));
    }
    m_ripple.pushSprite(RIP_X, RIP_Y, BG_BLACK);  // 黑色透明
}

// ====== 进度条（running 辅助动效）======
void DisplayPanel::renderProgressBar(uint32_t now, bool clearOnly) {
    m_tft.fillRect(10, PROG_Y, SCREEN_WIDTH - 20, 6, BG_BLACK);
    if (clearOnly) return;
    const uint32_t cycleMs = 2400;
    uint16_t fillW = (uint16_t)((float)(now % cycleMs) / cycleMs * (SCREEN_WIDTH - 20));
    m_tft.fillRoundRect(10, PROG_Y, fillW, 6, 3, rgb565(RGBColor(255, 200, 53)));
}

// ====== 上下棕黄边框：日期/时间 + WiFi/MQTT ======
void DisplayPanel::renderFrame() {
    // 上边框
    m_tft.fillRect(0, 0, SCREEN_WIDTH, FRAME_H, dim(FRAME_COLOR));
    // 日期（左，Font2 16px）
    m_tft.setTextFont(2);
    m_tft.setTextColor(dim(FRAME_TEXT), dim(FRAME_COLOR));
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
    // 时间（中，Font4 26px）
    m_tft.setTextFont(4);
    m_tft.setTextDatum(TR_DATUM);
    refreshClock();

    // 下边框
    renderStatusIndicators();
}

void DisplayPanel::renderStatusIndicators() {
    m_tft.fillRect(0, SCREEN_HEIGHT - FRAME_H, SCREEN_WIDTH, FRAME_H, dim(FRAME_COLOR));
    m_tft.setTextFont(2);
    m_tft.setTextDatum(TL_DATUM);
    m_tft.setTextColor(dim(FRAME_TEXT), dim(FRAME_COLOR));

    int x = 10;
    m_tft.setCursor(x, SCREEN_HEIGHT - 22);
    m_tft.print("WiFi");
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 15, 3, m_wifiOk ? TFT_GREEN : OFF_TEXT);
    x += 52;

    m_tft.setCursor(x, SCREEN_HEIGHT - 22);
    m_tft.print("MQTT");
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 15, 3, m_mqttOk ? TFT_GREEN : OFF_TEXT);
    x += 62;

    m_tft.setCursor(x, SCREEN_HEIGHT - 22);
    m_tft.print("NTP");
    bool ntpOk = time(nullptr) > 1600000000;
    m_tft.fillCircle(x + 9, SCREEN_HEIGHT - 15, 3, ntpOk ? TFT_GREEN : OFF_TEXT);

    if (m_dimmed) {
        m_tft.setTextColor(dim(OFF_TEXT), dim(FRAME_COLOR));
        m_tft.setTextDatum(TR_DATUM);
        m_tft.drawString("OFFLINE", SCREEN_WIDTH - 8, SCREEN_HEIGHT - 21);
        m_tft.setTextDatum(TL_DATUM);
    }
}

void DisplayPanel::refreshClock() {
    // 只刷新上边框右侧时间区域
    m_tft.fillRect(150, 2, SCREEN_WIDTH - 154, FRAME_H - 4, dim(FRAME_COLOR));
    m_tft.setTextFont(4);
    m_tft.setTextDatum(TR_DATUM);
    m_tft.setTextColor(dim(FRAME_TEXT), dim(FRAME_COLOR));
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

// ====== 全屏重绘（开机/变暗）======
void DisplayPanel::renderAll() {
    m_tft.fillScreen(BG_BLACK);
    renderFrame();
    renderText(millis());
    renderProgressBar(millis(), !isRunning());
    m_needFullRedraw = false;
}

// ====== 状态切换：只重绘变化区域（防闪烁）======
void DisplayPanel::renderChanged() {
    // 清掉旧动画残留（流水/涟漪/进度条区域）
    m_tft.fillRect(10, FLOW_Y, SCREEN_WIDTH - 20, FLOW_H, BG_BLACK);
    m_tft.fillRect(RIP_X, RIP_Y, RIP_S, RIP_S, BG_BLACK);
    renderProgressBar(millis(), !isRunning());
    renderText(millis());
    m_needChangedRedraw = false;
}

void DisplayPanel::update() {
    uint32_t now = millis();

    if (m_needFullRedraw) {
        renderAll();
        m_needChangedRedraw = false;
        m_lastAnimMs = now;
    } else if (m_needChangedRedraw) {
        renderChanged();
        m_lastAnimMs = now;
    }

    // 时钟：1 秒刷新
    if (now - m_lastClockMs >= 1000) {
        m_lastClockMs = now;
        refreshClock();
    }

    // 状态专属动效（~30fps）
    if (isRunning()) {
        if (now - m_lastAnimMs >= 33) {
            m_lastAnimMs = now;
            renderFlow(now);
            renderProgressBar(now, false);
        }
    } else if (isCmd()) {
        if (now - m_lastAnimMs >= 33) {
            m_lastAnimMs = now;
            renderRipple(now);
        }
    } else if (isWaiting()) {
        if (now - m_lastAnimMs >= 50) {
            m_lastAnimMs = now;
            renderText(now);
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