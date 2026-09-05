#include "DisplayPanel.h"
#include "ChineseFont.h"

// 看板配色（深色仪表盘风格）
#define BG_COLOR      0x0E1116  // 内容区背景
#define BAR_COLOR     0x1A2029  // 顶栏/底栏背景
#define BAR_TEXT      0xC8D0DC  // 栏内文字
#define BAR_TEXT_DIM  0x5A6472  // 弱化文字
#define FIG_COLOR     0xFFFFFF  // 前景文字

static constexpr int TOP_BAR_H = 26;
static constexpr int BOT_BAR_H = 22;
static constexpr int CARD_X = 16;
static constexpr int CARD_Y = 52;
static constexpr int CARD_S = 140;
static constexpr int TEX_X = CARD_X + CARD_S + 18;   // 174
static constexpr int TEX_Y = 56;
static constexpr int TEX_W = SCREEN_WIDTH - TEX_X - 6;  // 140
static constexpr int TEX_H = 92;
static constexpr int PROG_Y = 200;
static constexpr int PROG_H = 8;
static constexpr int DOT_X = 232;   // 顶栏状态圆点
static constexpr int DOT_Y = 13;

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
    m_tft.fillScreen(BG_COLOR);

    m_card.createSprite(CARD_S, CARD_S);
    m_card.setSwapBytes(true);
    m_text.createSprite(TEX_W, TEX_H);
    m_text.setSwapBytes(true);

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

uint16_t DisplayPanel::stateColor() const {
    if (m_cmdOverride) return m_cmdColor;
    if (m_state == nullptr) return rgb565(RGBColor(138, 147, 160));
    return rgb565(RGBColor(m_state->r, m_state->g, m_state->b));
}

bool DisplayPanel::isRunning() const {
    return !m_cmdOverride && m_state != nullptr && strcmp(m_state->key, "running") == 0;
}

void DisplayPanel::setState(const String& key, const String& message) {
    const StateDef* def = lookupState(key.c_str());
    if (def != m_state || m_message != message || m_cmdOverride) {
        m_state = def;
        m_message = message;
        m_cmdOverride = false;
        m_needChangedRedraw = true;
    }
}

void DisplayPanel::setCommandColor(uint16_t rgb565Color, const char* cmdName) {
    m_cmdOverride = true;
    m_cmdColor = rgb565Color;
    m_state = lookupState(cmdName);   // 仅取中文名/描述，颜色用命令色
    m_needChangedRedraw = true;
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

// ====== 卡片渲染（sprite 内：分类色 + 中文状态词 + 分类小字）======
void DisplayPanel::renderCard() {
    uint16_t color = dim(stateColor());
    m_card.fillRoundRect(0, 0, CARD_S, CARD_S, 18, color);
    m_card.drawRoundRect(6, 6, CARD_S - 12, CARD_S - 12, 14, rgb565(RGBColor(255, 255, 255)) & 0x7FFF);

    // 中央：中文状态词（24px）
    const char* cnName = m_state ? m_state->cnName : "未知状态";
    int bigX = (CARD_S - 24 * cnLen(cnName)) / 2;
    ChineseFont::draw(m_card, bigX, 42, cnName, dim(FIG_COLOR), true, 8);

    // 底部：分类小字（16px，半透明白）
    const char* cls = classCnName(m_state ? m_state->cls : StatusClass::UNKNOWN);
    int clsX = (CARD_S - 16 * cnLen(cls)) / 2;
    ChineseFont::draw(m_card, clsX, CARD_S - 30, cls, dim(FIG_COLOR & 0x7FFF), false, 8);

    m_card.pushSprite(CARD_X, CARD_Y);
}

// ====== 右侧文字面板（sprite 内）======
void DisplayPanel::renderTextPanel() {
    const char* cnName = m_state ? m_state->cnName : "未知状态";
    const char* cnDesc = m_state ? m_state->cnDesc : "未知状态";
    m_text.fillSprite(BG_COLOR);

    // 状态大字 24px（居中，白色）
    int bigW = 24 * cnLen(cnName);
    ChineseFont::draw(m_text, (TEX_W - bigW) / 2, 6, cnName, dim(FIG_COLOR), true, 8);

    // 描述 16px
    int descW = 16 * cnLen(cnDesc);
    ChineseFont::draw(m_text, (TEX_W - descW) / 2, 40, cnDesc, dim(BAR_TEXT_DIM), false, 16);

    // 消息 16px（单行，超宽截断）
    if (m_message.length() > 0) {
        ChineseFont::draw(m_text, 8, 68, m_message.c_str(), dim(BAR_TEXT), false, 9);
    }

    m_text.pushSprite(TEX_X, TEX_Y);
}

// ====== 顶栏 ======
void DisplayPanel::renderTopBar() {
    m_tft.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_H, BAR_COLOR);
    m_tft.setTextFont(2);
    m_tft.setTextColor(BAR_TEXT, BAR_COLOR);
    m_tft.setCursor(10, 8);
    m_tft.print("AI ");
    ChineseFont::draw(m_tft, 40, 5, "工作流状态", BAR_TEXT, BAR_COLOR, false, 8);

    m_tft.fillCircle(DOT_X, DOT_Y, 5, dim(stateColor()));
    refreshClock();
}

// ====== 进度条（running 动效）======
void DisplayPanel::renderProgressBar(uint32_t now, bool clearOnly) {
    m_tft.fillRect(16, PROG_Y, SCREEN_WIDTH - 32, PROG_H, dim(BG_COLOR));
    if (clearOnly) return;
    const uint32_t cycleMs = 2000;
    uint16_t fillW = (uint16_t)((float)(now % cycleMs) / cycleMs * (SCREEN_WIDTH - 32));
    uint16_t color = dim(stateColor());
    m_tft.drawRoundRect(16, PROG_Y, SCREEN_WIDTH - 32, PROG_H, PROG_H / 2, dim(BAR_TEXT_DIM));
    if (fillW > 0) {
        m_tft.fillRoundRect(16, PROG_Y, fillW, PROG_H, PROG_H / 2, color);
    }
}

// ====== 底栏 ======
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

// ====== 全屏重绘（仅开机/变暗时）======
void DisplayPanel::renderAll() {
    m_tft.fillScreen(dim(BG_COLOR));
    renderTopBar();
    renderCard();
    renderTextPanel();
    renderProgressBar(millis(), !isRunning());
    renderStatusBar();
    m_needFullRedraw = false;
}

// ====== 状态切换：只重绘变化的区域（防闪烁）======
void DisplayPanel::renderChanged() {
    renderCard();                    // 卡片 Sprite 原子推屏
    renderTextPanel();               // 文字 Sprite 原子推屏
    m_tft.fillCircle(DOT_X, DOT_Y, 5, dim(stateColor()));  // 顶栏状态圆点
    renderProgressBar(millis(), !isRunning());
    m_needChangedRedraw = false;
}

void DisplayPanel::update() {
    uint32_t now = millis();

    if (m_needFullRedraw) {
        renderAll();
        m_needChangedRedraw = false;
    } else if (m_needChangedRedraw) {
        renderChanged();
    }

    // 时钟：1 秒刷新
    if (now - m_lastClockMs >= 1000) {
        m_lastClockMs = now;
        refreshClock();
    }

    // running 进度条动画
    if (isRunning()) {
        renderProgressBar(now, false);
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