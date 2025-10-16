#ifndef POMODORO_H
#define POMODORO_H

#ifndef IRAM_ATTR
  #define IRAM_ATTR
#endif

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Bounce2.h>
#include <limits.h>
#include <math.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <esp_sleep.h>
  #include <driver/gpio.h>
  #include <driver/rtc_io.h>
#else
  #error "This firmware targets ESP32-S3 boards. Select an ESP32-S3 board before compiling."
#endif

// TFT (SPI)
#define TFT_CS    11 // 배선 연결시 CS 와 DC 를 바꿀 것
#define TFT_DC    10 // 왜인지 표기가 잘못되어 있는 듯;
#define TFT_RST   9   // or set to -1 and tie RESET high
#define TFT_SDA   12
#define TFT_SCL   13

// ROTARY ENCODER (A/B) + BUTTON (PUSH)
#define ENC_A     6
#define ENC_B     7
#define ENC_BTN   5

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

extern Adafruit_GC9A01A tft;
extern Bounce btnDebounce;

constexpr int16_t W = 240;
constexpr int16_t H = 240;
constexpr int16_t CX = W / 2;
constexpr int16_t CY = H / 2;
constexpr int16_t R_OUT = 116;

constexpr uint16_t COL_PRIMARY = rgb565(255, 107, 87);

constexpr uint16_t COL_BG        = 0xfffa;
constexpr uint16_t COL_BG_DARK   = 0xeed7;
constexpr uint16_t COL_BG_PAUSED = rgb565(255, 235, 235);
constexpr uint16_t COL_DARK      = 0x39E7;
constexpr uint16_t COL_BLACK     = 0x0000;
constexpr uint16_t COL_RED       = COL_PRIMARY;
constexpr uint16_t COL_RED_DARK  = 0xe2a8;
constexpr uint16_t COL_LIGHTRED  = 0xFBE0;
constexpr uint16_t COL_WHITE     = 0xFFFF;

constexpr uint32_t RUN_REPAINT_MS         = 1000;
constexpr uint32_t PAUSE_BLINK_WHITE_MS   = 400;
constexpr uint32_t PAUSE_BLINK_BLACK_MS   = 600;
constexpr uint32_t PAUSE_BLINK_FRAME_MS   = 40;
constexpr uint32_t PAUSE_SLEEP_DELAY_MS   = 180000UL;
constexpr uint32_t ENCODER_THROTTLE_MS    = 1000;
constexpr uint32_t SETTING_ANIM_DURATION_MS = 300;
constexpr uint32_t CENTER_DISPLAY_MS      = 2000;
constexpr uint8_t  TIMEOUT_BLINK_COUNT    = 5;
constexpr uint8_t  OPTION_COUNT           = 4;
constexpr uint8_t  CENTER_CLEAR_PADDING   = 6;

constexpr uint8_t OPTIONS[OPTION_COUNT] = {15, 30, 60, 0};
constexpr float   SETTING_ANIM_EPSILON    = 1e-4f;

inline float deg2rad(float d) { return d * (PI / 180.0f); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline uint8_t digitalReadFast(uint8_t pin) { return digitalRead(pin); }

struct PomodoroState;

inline void resetBlink(PomodoroState &st, uint32_t now);

enum class Mode { SETTING, RUNNING, PAUSED, TIMEOUT, SLEEPING };

struct EncoderState {
  volatile int8_t steps = 0;
  volatile uint8_t prev = 0;
  volatile int8_t quarter = 0;
};

float easeLinear(float t);

using EaseFn = float (*)(float);

struct FloatTween {
  float from = 0.0f;
  float to = 0.0f;
  uint32_t start = 0;
  uint32_t duration = 0;
  EaseFn ease = easeLinear;
  bool active = false;

  void snapTo(float value) {
    from = to = value;
    start = 0;
    duration = 0;
    ease = easeLinear;
    active = false;
  }

  void startTween(float fromValue, float toValue, uint32_t startMs, uint32_t durationMs, EaseFn easeFn = nullptr) {
    from = fromValue;
    to = toValue;
    start = startMs;
    duration = durationMs;
    ease = easeFn ? easeFn : easeLinear;
    active = (durationMs > 0) && (fabsf(toValue - fromValue) > SETTING_ANIM_EPSILON);
    if (!active) {
      snapTo(toValue);
    }
  }

  float sample(uint32_t nowMs) {
    if (!active) {
      return to;
    }

    uint32_t elapsed = (nowMs >= start) ? (nowMs - start) : 0;
    float t = (duration == 0) ? 1.0f : clampf(static_cast<float>(elapsed) / static_cast<float>(duration), 0.0f, 1.0f);
    float eased = ease ? ease(t) : t;
    float value = lerpf(from, to, eased);

    if (t >= 1.0f - SETTING_ANIM_EPSILON || elapsed >= duration) {
      snapTo(to);
      value = to;
    }

    return value;
  }

  bool isActive() const { return active; }
};

struct PomodoroState {
  Mode mode = Mode::SETTING;
  uint8_t optionIndex = 0;
  uint32_t lastInputMs = 0;
  uint32_t stateTs = 0;
  uint32_t runStartMs = 0;
  uint32_t runDurationMs = 0;
  uint32_t pausedAtMs = 0;
  bool blinkOn = false;
  uint32_t blinkTs = 0;
  uint32_t blinkDurationMs = 0;
  uint32_t blinkFrameTs = 0;
  float blinkFromLevel = 0.0f;
  float blinkToLevel = 0.0f;
  uint32_t lastEncoderMs = 0;
  float settingFracCurrent = 0.0f;
  float settingFracTarget = 0.0f;
  FloatTween settingTween;
  uint32_t centerDisplayUntilMs = 0;
  uint8_t centerDisplayValue = 0;
  bool pendingTimeout = false;
};

struct DisplayDialCache {
  bool wedgeValid = false;
  float wedgeEndDeg = 0.0f;
  float prevWedgeEndDeg = 0.0f;
  uint16_t wedgeColor = COL_BG;

  bool pointerValid = false;
  float pointerAngleDeg = 0.0f;

  bool blinkVisible = false;
  float blinkAngleDeg = 0.0f;
  int16_t blinkX = 0;
  int16_t blinkY = 0;
};

struct DisplayState {
  bool isAwake = true;
  DisplayDialCache dial;
};

extern EncoderState gEncoder;
extern PomodoroState gState;
extern DisplayState gDisplay;

float easeIn(float t);
float easeOut(float t);
float easeInOut(float t);
float cubicBezierValue(float t, float p0, float p1, float p2, float p3);
float cubicBezierEase(float x, float x1, float y1, float x2, float y2);

uint8_t currentMinutes(const PomodoroState &st);
uint32_t computeElapsedMs(const PomodoroState &st, uint32_t now);
uint32_t computeRemainingMs(const PomodoroState &st, uint32_t now);

void handleEncoderInput(PomodoroState &st);
void handleButtonInput(PomodoroState &st);
void updateStateMachine(PomodoroState &st, uint32_t now);

void enterSetting(PomodoroState &st);
void resumeRun(PomodoroState &st);
void enterPaused(PomodoroState &st);
void enterTimeout(PomodoroState &st);
void goToSleep(PomodoroState &st);

void renderAll(PomodoroState &st, bool forceBg = false, uint32_t now = UINT32_MAX);
void drawDialBackground(uint16_t bgColor = COL_BG, bool clearAll = false);
void drawRemainingWedge(float remainingSec, float totalSec, bool paused, uint16_t bgColor = COL_BG);
void drawMinuteHand(float remainingSec, float totalSec,
                    uint16_t bgColor = COL_BG,
                    uint16_t pointerColor = COL_RED_DARK,
                    uint16_t hubColor = COL_RED);
void drawBlinkingTip(float remainingSec, float totalSec, bool on, uint16_t bgColor = COL_BG);
void showCenterText(const String &s, uint8_t textSize, uint16_t color = COL_RED, uint16_t bg = COL_BG);
void showCenterText(const char *s, uint8_t textSize, uint16_t color = COL_RED, uint16_t bg = COL_BG);
void drawCenterText(const String &s, uint16_t bg = COL_BG);
void fillArc(Adafruit_GFX& gfx,
             int16_t cx, int16_t cy,
             int16_t r_inner, int16_t r_outer,
             float a0_deg, float a1_deg,
             uint16_t color,
             float step_deg = 3.0f);
void fillSector(Adafruit_GFX& gfx,
                int16_t cx, int16_t cy, int16_t r,
                float a0_deg, float a1_deg,
                uint16_t color,
                float step_deg = 2.0f);
void drawThickLine(Adafruit_GFX &gfx,
                   int16_t x0, int16_t y0,
                   int16_t x1, int16_t y1,
                   uint16_t color,
                   uint8_t thickness = 3);

void IRAM_ATTR onEncChange();
void IRAM_ATTR wakeupFromButton();
void wakeDummy();
void configureLightSleepWakeup();
void tftEnterSleepSeqSoftOnly();
void tftExitSleepSeqSoftOnly();

inline void resetDisplayCache(DisplayState &disp) {
  disp.dial = DisplayDialCache{};
}

inline void resetBlink(PomodoroState &st, uint32_t now) {
  st.blinkTs = now;
  st.blinkOn = false;
  st.blinkDurationMs = 0;
  st.blinkFrameTs = now;
  st.blinkFromLevel = 0.0f;
  st.blinkToLevel = 0.0f;
}

#endif  // POMODORO_H
