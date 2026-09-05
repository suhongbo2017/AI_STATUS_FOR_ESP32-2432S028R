#include "DisplayPanel.h"
#include "ChineseFont.h"

// 看板配色（深色仪表盘风格）
#define BG_COLOR      0x0E1116  // 内容区背景
#define BAR_COLOR     0x1A2029  // 顶栏/底栏背景
#define BAR_TEXT      0xC8D0DC  // 栏内文字
#define BAR_TEXT_DIM  0x5A6472  // 弱化文字
#define FIG_COLOR     0xFFFFFF  // 火柴人颜色
#define GROUND_COLOR  0x7BEF    // 地面线（半透明白）

static constexpr int TOP_BAR_H = 26;
static constexpr int BOT_BAR_H = 22;
static constexpr int CARD_X = 16;
static constexpr int CARD_Y = 52;
static constexpr int CARD_S = 140;
static constexpr int FIG_W = 48;
static constexpr int FIG_H = 52;
static constexpr int TEX_X = CARD_X + CARD_S + 18;   // 166
static constexpr int TEX_Y = 56;
static constexpr int TEX_W = SCREEN_WIDTH - TEX_X - 6;  // 148
static constexpr int TEX_H = 92;
static constexpr int PROG_Y = 200;
static constexpr int PROG_H = 8;

// 状态 → 中文/姿势 映射（key 为内部名大写）
static const StateInfo STATE_TABLE[] = {
    { "INIT",      "初始化",   "系统启动中", FigurePose::WALK },
    { "IDLE",      "空闲",     "等待任务",   FigurePose::STAND },
    { "RUNNING",   "运行中",   "正在处理",   FigurePose::RUN },
    { "DONE",      "已完成",   "任务完成",   FigurePose::SIT },
    { "ERROR",     "出错",     "任务出错",   FigurePose::LIE },
    { "WAITING",   "等待",     "等待输入",   FigurePose::WAVE },
    { "THROTTLED", "冷却",     "限流冷却",   FigurePose::WALK },
    { "CRITICAL",  "严重故障", "请检查连接", FigurePose::LIE },
    { "CMD",       "手动控制", "手动控制",   FigurePose::STAND },
};

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

const StateInfo* DisplayPanel::currentState() const {
    return lookupState(m_stateName);
}

const StateInfo* DisplayPanel::lookupState(const String& upperName) {
    for (const auto& s : STATE_TABLE) {
        if (upperName == s.key) return &s;
    }
    return &STATE_TABLE[1];  // 默认 IDLE
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

// ====== 火柴人（48x52，地面 y=47，基准点 cx=24）======
void DisplayPanel::drawFigure(const StateInfo& info, uint32_t now) {
    float t = now / 1000.0f;
    FigurePose pose = info.pose;
    const int cx = 24;

    // 跑/走：弹跳 + 腿部摆角
    float swing = 0, freq = 1.0f, bounce = 0;
    if (pose == FigurePose::RUN)  { swing = 0.75f; freq = 2.2f; }
    if (pose == FigurePose::WALK) { swing = 0.35f; freq = 1.1f; }
    if (pose == FigurePose::RUN || pose == FigurePose::WALK) {
        bounce = fabsf(sinf(2.0f * PI * freq * t)) * (pose == FigurePose::RUN ? 2.5f : 1.5f);
    }
    int offY = -(int)bounce;
    float a = swing * sinf(2.0f * PI * freq * t);

    const int groundY = 47;
    const int headY = 10 + offY;
    const int hipY = 30 + offY;
    const int shY = 18 + offY;

    m_card.drawFastHLine(0, groundY, FIG_W, dim(GROUND_COLOR));
    m_card.fillCircle(cx, headY, 5, dim(FIG_COLOR));
    m_card.drawLine(cx, headY + 5, cx, hipY, dim(FIG_COLOR));

    switch (pose) {
        case FigurePose::SIT: {
            // 坐姿休息：腿前伸，手扶膝
            m_card.drawLine(cx, hipY, cx + 17, hipY, dim(FIG_COLOR));
            m_card.drawLine(cx - 1, hipY, cx - 8, hipY + 7, dim(FIG_COLOR));
            m_card.drawLine(cx, shY + 4, cx + 13, hipY - 2, dim(FIG_COLOR));
            break;
        }
        case FigurePose::LIE: {
            // 躺倒：身体水平，腿分叉，臂上举
            m_card.fillCircle(8, 42, 5, dim(FIG_COLOR));
            m_card.drawLine(13, 42, 33, 42, dim(FIG_COLOR));
            m_card.drawLine(33, 42, 42, 46, dim(FIG_COLOR));
            m_card.drawLine(33, 42, 40, 38, dim(FIG_COLOR));
            m_card.drawLine(17, 42, 9, 33, dim(FIG_COLOR));
            m_card.drawLine(23, 42, 28, 32, dim(FIG_COLOR));
            break;
        }
        case FigurePose::WAVE: {
            // 站立挥手：左手自然下垂，右手举起摆动
            m_card.drawLine(cx, hipY, cx + 3, groundY, dim(FIG_COLOR));
            m_card.drawLine(cx, hipY, cx - 3, groundY, dim(FIG_COLOR));
            m_card.drawLine(cx, shY, cx - 8, shY + 12, dim(FIG_COLOR));
            float w = sinf(2.0f * PI * 2.0f * t) * 4;
            m_card.drawLine(cx, shY, cx + 10, shY - 12 + (int)w, dim(FIG_COLOR));
            break;
        }
        default: {
            // STAND / WALK / RUN：双腿反相摆动，手臂随动
            float a2 = -a;
            int lx1 = cx + (int)(sinf(a) * 17);
            int ly1 = hipY + (int)(cosf(a) * 17);
            int lx2 = cx + (int)(sinf(a2) * 17);
            int ly2 = hipY + (int)(cosf(a2) * 17);
            m_card.drawLine(cx, hipY, lx1, ly1, dim(FIG_COLOR));
            m_card.drawLine(cx, hipY, lx2, ly2, dim(FIG_COLOR));

            float arm = -a * 0.9f;
            m_card.drawLine(cx, shY, cx + (int)(sinf(arm) * 11), shY + (int)(cosf(arm) * 11), dim(FIG_COLOR));
            m_card.drawLine(cx, shY, cx - (int)(sinf(arm) * 11), shY + (int)(cosf(arm) * 11), dim(FIG_COLOR));
            break;
        }
    }
}

// ====== 卡片渲染（sprite 内：状态色 + 火柴人 + 中文状态词）======
void DisplayPanel::renderCard(uint32_t now) {
    uint16_t color = dim(rgb565(m_statusColor));
    m_card.fillRoundRect(0, 0, CARD_S, CARD_S, 18, color);
    m_card.drawRoundRect(6, 6, CARD_S - 12, CARD_S - 12, 14, rgb565(RGBColor::White) & 0x7FFF);

    const StateInfo* st = currentState();
    drawFigure(*st, now);

    // 卡片底部：中文状态词（16px 点阵）
    int textX = (CARD_S - 16 * cnLen(st->cnName)) / 2;
    ChineseFont::draw(m_card, textX, CARD_S - 26, st->cnName, dim(FIG_COLOR), false, 8);

    m_card.pushSprite(CARD_X, CARD_Y);
}

// ====== 右侧文字面板（sprite 内）======
void DisplayPanel::renderTextPanel() {
    const StateInfo* st = currentState();
    m_text.fillSprite(BG_COLOR);

    // 状态大字 24px（居中，白色）
    int bigW = 24 * cnLen(st->cnName);
    ChineseFont::draw(m_text, (TEX_W - bigW) / 2, 6, st->cnName, dim(FIG_COLOR), true, 8);

    // 描述 16px
    int descW = 16 * cnLen(st->cnDesc);
    ChineseFont::draw(m_text, (TEX_W - descW) / 2, 40, st->cnDesc, dim(BAR_TEXT_DIM), false, 16);

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

    // 状态色圆点
    m_tft.fillCircle(232, 13, 5, rgb565(m_statusColor));
    refreshClock();
}

// ====== 进度条（running 动效）======
void DisplayPanel::renderProgressBar(uint32_t now, bool clearOnly) {
    m_tft.fillRect(16, PROG_Y, SCREEN_WIDTH - 32, PROG_H, dim(BG_COLOR));
    if (clearOnly) return;
    const uint32_t cycleMs = 2000;
    uint16_t fillW = (uint16_t)((float)(now % cycleMs) / cycleMs * (SCREEN_WIDTH - 32));
    uint16_t color = dim(rgb565(m_statusColor));
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

// ====== 全屏重绘（仅状态/消息/变暗变化时）======
void DisplayPanel::renderAll() {
    m_tft.fillScreen(dim(BG_COLOR));
    renderTopBar();
    renderCard(millis());
    renderTextPanel();
    renderProgressBar(millis(), m_stateName != "RUNNING");
    renderStatusBar();
    m_needFullRedraw = false;
}

void DisplayPanel::update() {
    uint32_t now = millis();

    if (m_needFullRedraw) {
        renderAll();
        m_lastAnimMs = now;
    }

    // 时钟：1 秒刷新
    if (now - m_lastClockMs >= 1000) {
        m_lastClockMs = now;
        refreshClock();
    }

    // 卡片动画：~12fps（重画整卡 sprite，含火柴人与中文词）
    if (now - m_lastAnimMs >= 80) {
        m_lastAnimMs = now;
        renderCard(now);
    }

    // running 进度条
    if (m_stateName == "RUNNING") {
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