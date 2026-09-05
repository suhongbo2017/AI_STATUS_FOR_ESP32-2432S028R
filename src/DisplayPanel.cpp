#include "DisplayPanel.h"
#include "ChineseFont.h"

// 配色
#define FRAME_COLOR  0xFFFF    // 上下边框：纯白
#define FRAME_TEXT   0x0000    // 边框内文字：黑
#define OFF_TEXT     0xCC0000  // 离线警示（白底上的深红）

// 边框：上下各 12% 屏高（240*12% ≈ 29px）
static constexpr int FRAME_H = 29;
static constexpr int MID_Y = FRAME_H;                   // 中间区上界 29
static constexpr int MID_BOT = SCREEN_HEIGHT - FRAME_H; // 中间区下界 211
static constexpr int MID_H = MID_BOT - MID_Y;           // 182

// 自动息屏：超过该时长无任何 ai/status 消息则关背光（可被 build_flags 覆盖，测试用短值）
#ifndef SCREEN_TIMEOUT_MS
#define SCREEN_TIMEOUT_MS (10UL * 60 * 1000)
#endif

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
    m_lastActivityMs = millis();

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
    (void)message;  // 当前 UI 只显示状态大字，消息暂不展示（协议层保留解析）
    const StateDef* def = lookupState(key.c_str());
    if (def != m_state) {
        m_state = def;
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
        // 离线不改变状态色（避免深色近黑无法辨认），仅底栏显示 OFFLINE 标记
        renderStatusIndicators();
    }
}

void DisplayPanel::notifyActivity() {
    m_lastActivityMs = millis();
    if (m_screenOff) {
        // 唤醒：开背光 + 全量重绘恢复画面
        m_screenOff = false;
        digitalWrite(TFT_BL, HIGH);
        m_needFullRedraw = true;
    }
}

// ====== 中间状态块（纯色背景 + 72px 状态大字）======
void DisplayPanel::renderMid(uint32_t now, bool force) {
    if (m_state == nullptr) return;

    uint16_t bg = stateBg(1.0f);
    m_tft.fillRect(0, MID_Y, SCREEN_WIDTH, MID_H, bg);
    uint16_t fg = textFg();

    // 状态大字 60px 统一（居中，5 字 300px 亦不溢出）
    int len = cnLen(m_state->cnName);
    int bigW = 60 * len;
    ChineseFont::draw(m_tft, (SCREEN_WIDTH - bigW) / 2,
                      MID_Y + (MID_H - 60) / 2, m_state->cnName, fg, bg, 60, 6);

    // running：重置充电进度条（下一帧全量重绘）
    if (isRunning()) {
        m_chargeReset = true;
    }

    // waiting：文字下方打字三点动画
    if (isWaiting()) {
        renderDots(now);
    }
}

// ====== waiting 打字三点动画（青底黑点依次亮起，经典等待输入语义）======
void DisplayPanel::renderDots(uint32_t now) {
    const int dotW = 56;                                    // 三点总宽
    const int dotY = MID_Y + MID_H - 46;                    // 大字下方
    const int dx = 20;
    uint16_t bg = stateBg(1.0f);

    m_tft.fillRect((SCREEN_WIDTH - dotW) / 2 - 4, dotY - 6, dotW + 8, 12, bg);
    int phase = (now / 400) % 4;   // 0:000 1:100 2:110 3:111
    for (int i = 0; i < 3; i++) {
        if (i < phase) {
            m_tft.fillCircle((SCREEN_WIDTH - dotW) / 2 + 9 + i * dx, dotY, 5, FRAME_TEXT);
        }
    }
}

// ====== running 充电式格状进度条（粗 40px，黑底 + 分段黄格依次点亮）======
// 增量绘制：格子数量变化时才重绘，避免整条刷新闪烁
#define CHARGE_CELLS 14
#define CHARGE_CYCLE_MS 3000
#define CHARGE_BAR_W 260
#define CHARGE_BAR_H 40
#define CHARGE_CELL_W 14
#define CHARGE_CELL_GAP 4
#define CHARGE_CELL_ON 0x07E0  // 亮格（黄）
#define CHARGE_CELL_OFF 0x222222 // 暗格（底）

static constexpr int CHARGE_X = (SCREEN_WIDTH - CHARGE_BAR_W) / 2;
static constexpr int CHARGE_Y = MID_Y + MID_H - 46;  // 163..203，在大字下方

void DisplayPanel::renderCharge(uint32_t now) {
    int cur = (int)((now % CHARGE_CYCLE_MS) * (long)CHARGE_CELLS / CHARGE_CYCLE_MS);
    if (cur > CHARGE_CELLS) cur = CHARGE_CELLS;

    if (m_chargeReset || cur < m_lastCells) {
        // 全量重绘：外框 + 全部暗格
        m_tft.fillRoundRect(CHARGE_X, CHARGE_Y, CHARGE_BAR_W, CHARGE_BAR_H, 10, TFT_BLACK);
        for (int i = 0; i < CHARGE_CELLS; i++) {
            int x = CHARGE_X + 6 + i * (CHARGE_CELL_W + CHARGE_CELL_GAP);
            m_tft.fillRoundRect(x, CHARGE_Y + 6, CHARGE_CELL_W, CHARGE_BAR_H - 12, 4, CHARGE_CELL_OFF);
        }
        m_lastCells = 0;
        m_chargeReset = false;
        if (cur == 0) return;
    }

    // 仅点亮新增的格子
    while (m_lastCells < cur) {
        int x = CHARGE_X + 6 + m_lastCells * (CHARGE_CELL_W + CHARGE_CELL_GAP);
        m_tft.fillRoundRect(x, CHARGE_Y + 6, CHARGE_CELL_W, CHARGE_BAR_H - 12, 4, CHARGE_CELL_ON);
        m_lastCells++;
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
    const int yText = MID_BOT + (FRAME_H - 16) / 2;  // Font2 16px 文字垂直居中
    const int yDot = MID_BOT + FRAME_H / 2;          // 圆点垂直居中
    m_tft.setTextFont(2);
    m_tft.setTextDatum(TL_DATUM);
    m_tft.setTextColor(FRAME_TEXT, FRAME_COLOR);

    // 文字（宽 = 字符数*8px）→ 间隙 8px → 圆点 r4 → 组间距
    int x = 12;
    m_tft.setCursor(x, yText);
    m_tft.print("WiFi");
    x += 4 * 8 + 8;
    m_tft.fillCircle(x, yDot, 4, m_wifiOk ? TFT_GREEN : TFT_RED);
    x += 4 + 12;

    m_tft.setCursor(x, yText);
    m_tft.print("MQTT");
    x += 5 * 8 + 8;
    m_tft.fillCircle(x, yDot, 4, m_mqttOk ? TFT_GREEN : TFT_RED);
    x += 4 + 12;

    m_tft.setCursor(x, yText);
    m_tft.print("NTP");
    x += 3 * 8 + 8;
    bool ntpOk = time(nullptr) > 1600000000;
    m_tft.fillCircle(x, yDot, 4, ntpOk ? TFT_GREEN : TFT_RED);

    if (m_dimmed) {
        m_tft.setTextColor(OFF_TEXT, FRAME_COLOR);
        m_tft.setTextDatum(TR_DATUM);
        m_tft.drawString("OFFLINE", SCREEN_WIDTH - 8, yText - 1);
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

    // 自动息屏：超时无活动 → 关背光、灭 LED；后续帧跳过渲染直到被唤醒
    if (!m_screenOff && now - m_lastActivityMs >= SCREEN_TIMEOUT_MS) {
        m_screenOff = true;
        digitalWrite(TFT_BL, LOW);
        ledWrite(LED_BLUE_PIN, false);
        ledWrite(LED_GREEN_PIN, false);
        Serial.println("[屏幕] 无活动，已息屏");
    }
    if (m_screenOff) return;

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
        if (now - m_lastAnimMs >= 33) {   // 充电进度 30fps
            m_lastAnimMs = now;
            renderCharge(now);
        }
    } else if (isWaiting()) {
        if (now - m_lastAnimMs >= 400) {  // 打字三点：2.5 次/秒
            m_lastAnimMs = now;
            renderDots(now);
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