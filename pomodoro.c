/*
  ESP32-S3 Pomodoro Timer on 1.28" Round TFT (240x240, GC9A01A)
  - Rotary encoder: cycles minutes CW 15→30→60→0→15 (CCW reversed)
  - Click to pause/resume
  - UI: gray background, remaining time as red wedge + minute hand
  - While paused: lighter red, arc tip blinks, remaining minutes shown at center
  - Idle after pause 3 min → sleep
  - Timeout (0) → blink "0" 5x → sleep
  - Setting flow: after last encoder movement, wait 0.5s → show (value-1) 0.5s → hide number → start timer

  Board: ESP32-S3 (3.3V)
  Libraries (Library Manager):
    - Adafruit GFX Library
    - Adafruit GC9A01A (a.k.a. Adafruit_GC9A01A)
    - Bounce2
  - No additional low-power library required (uses native ESP32 light sleep API)

  Pin mapping below can be edited to your wiring.
*/

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

struct PomodoroState;
struct EncoderState;

/************ PIN CONFIG ************/
// TFT (SPI)
#define TFT_CS    2
#define TFT_DC    1
#define TFT_RST   3   // or set to -1 and tie RESET high
#define TFT_SDA   4
#define TFT_SCL   5

// ROTARY ENCODER (A/B) + BUTTON (PUSH)
#define ENC_A     9
#define ENC_B     10
#define ENC_BTN   11

/************ DISPLAY ************/
// 24-bit RGB to 16-bit RGB565
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
Adafruit_GC9A01A tft(&SPI, TFT_CS, TFT_DC, TFT_RST);

constexpr int16_t W = 240;
constexpr int16_t H = 240;
constexpr int16_t CX = W / 2;
constexpr int16_t CY = H / 2;
constexpr int16_t R_OUT = 116;

// RED
constexpr uint16_t COL_PRIMARY = rgb565(255, 107, 87);

constexpr uint16_t COL_BG        = 0xfffa;
constexpr uint16_t COL_BG_DARK   = 0xeed7;
constexpr uint16_t COL_DARK      = 0x39E7;
constexpr uint16_t COL_RED       = COL_PRIMARY;
constexpr uint16_t COL_RED_DARK  = 0xe2a8;
constexpr uint16_t COL_LIGHTRED  = 0xFBE0;
constexpr uint16_t COL_WHITE     = 0xFFFF;

constexpr uint32_t PREROLL_DELAY_MS       = 500;
constexpr uint32_t PREROLL_HIDE_MS        = 500;
constexpr uint32_t RUN_REPAINT_MS         = 1000;
constexpr uint32_t PAUSE_BLINK_MS         = 500;
constexpr uint32_t PAUSE_SLEEP_DELAY_MS   = 180000UL;
constexpr uint32_t ENCODER_THROTTLE_MS    = 500;
constexpr uint32_t SETTING_ANIM_DURATION_MS = 300;
constexpr uint8_t  TIMEOUT_BLINK_COUNT    = 5;
constexpr uint8_t  OPTION_COUNT           = 4;
constexpr uint8_t  CENTER_CLEAR_PADDING   = 6;

constexpr uint8_t OPTIONS[OPTION_COUNT] = {15, 30, 60, 0};
constexpr float   SETTING_ANIM_EPSILON    = 1e-4f;

enum class Mode { SETTING, PREROLL_SHOW, PREROLL_HIDE, RUNNING, PAUSED, TIMEOUT, SLEEPING };

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
  Mode mode = Mode::SETTING;        // 현재 동작 모드(설정, 실행, 일시정지 등)
  uint8_t optionIndex = 0;          // OPTIONS 배열에서 선택된 타이머 길이의 인덱스
  uint32_t lastInputMs = 0;         // 마지막 인코더 입력이 발생한 시간 (millis)
  uint32_t stateTs = 0;             // 현재 모드로 전환된 시각 (millis)
  uint32_t runStartMs = 0;          // 타이머가 실제로 시작된 기준 시각 (재시작 시 보정됨)
  uint32_t runDurationMs = 0;       // 현재 타이머의 전체 길이 (밀리초)
  uint32_t pausedAtMs = 0;          // 일시정지에 진입한 시각 (millis)
  bool blinkOn = false;             // 깜박임 UI 표현에 사용할 토글 플래그
  uint32_t blinkTs = 0;             // 마지막으로 깜박임/화면 갱신을 수행한 시각 (millis)
  uint32_t lastEncoderMs = 0;       // 마지막으로 인코더 입력을 반영한 시각 (millis)
  float settingFracCurrent = 0.0f;  // 설정 화면에서 현재 표시 중인 호 비율
  float settingFracTarget = 0.0f;   // 설정 화면에서 목표 호 비율
  FloatTween settingTween;          // 설정 화면 애니메이션 트윈
};

struct DisplayState {
  bool isAwake = true;
};

static EncoderState gEncoder;
static PomodoroState gState;
static DisplayState gDisplay;
static Bounce btnDebounce;

/************ FORWARD DECLS ************/
static inline float deg2rad(float d) { return d * (PI / 180.0f); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline void resetBlink(PomodoroState &st, uint32_t now) { st.blinkTs = now; st.blinkOn = false; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

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
void enterPreRollShow(PomodoroState &st);
void enterPreRollHide(PomodoroState &st);
void startRunFromSelection(PomodoroState &st);
void resumeRun(PomodoroState &st);
void enterPaused(PomodoroState &st);
void enterTimeout(PomodoroState &st);
void goToSleep(PomodoroState &st);

void renderAll(PomodoroState &st, bool forceBg = false, uint32_t now = UINT32_MAX);
void drawDialBackground(bool clearAll);
void drawRemainingWedge(float remainingSec, float totalSec, bool paused);
void drawMinuteHand(float remainingSec, float totalSec);
void drawBlinkingTip(float remainingSec, float totalSec, bool on);
void showCenterText(const String &s, uint8_t textSize, uint16_t color = COL_RED, uint16_t bg = COL_BG);
void showCenterText(const char *s, uint8_t textSize, uint16_t color = COL_RED, uint16_t bg = COL_BG);
void drawCenterText(const String &s);
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
void configureLightSleepWakeup();

/************ INTERRUPTS ************/
void IRAM_ATTR onEncChange() {

  uint8_t a = digitalReadFast(ENC_A);
  uint8_t b = digitalReadFast(ENC_B);
  uint8_t enc = (a << 1) | b;
  static const int8_t tab[16] = { 0, -1, 1, 0,
                                  1,  0, 0, -1,
                                 -1,  0, 0,  1,
                                  0,  1, -1, 0 };
  gEncoder.prev = ((gEncoder.prev << 2) | enc) & 0x0F;
  int8_t delta = tab[gEncoder.prev];
  if (delta) {
    gEncoder.quarter += delta;
    if (gEncoder.quarter >= 4) {
      gEncoder.quarter = 0;
      gEncoder.steps++;
    } else if (gEncoder.quarter <= -4) {
      gEncoder.quarter = 0;
      gEncoder.steps--;
    }
  }
}

void IRAM_ATTR wakeupFromButton() {
  // Intentionally empty; wake-up handled by light sleep configuration.
}

void wakeDummy() {
  // empty; used only as wake ISR
}

/************ SETUP ************/
void setup() {

  Serial.begin(115200);
  delay(50);

  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(TFT_DC, OUTPUT);

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);

  btnDebounce.attach(ENC_BTN, INPUT_PULLUP);
  btnDebounce.interval(15);

  SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS); 
  SPI.setFrequency(40000000); // 20MHz

  tft.begin();

  tft.setRotation(0);
  tft.fillScreen(COL_BG);

  gState.optionIndex = 0;
  enterSetting(gState);

  if (digitalPinToInterrupt(ENC_A) != NOT_AN_INTERRUPT) {
    attachInterrupt(digitalPinToInterrupt(ENC_A), onEncChange, CHANGE);
  }
  if (digitalPinToInterrupt(ENC_B) != NOT_AN_INTERRUPT) {
    attachInterrupt(digitalPinToInterrupt(ENC_B), onEncChange, CHANGE);
  }
  if (digitalPinToInterrupt(ENC_BTN) != NOT_AN_INTERRUPT) {
    attachInterrupt(digitalPinToInterrupt(ENC_BTN), wakeupFromButton, FALLING);
  }


  configureLightSleepWakeup();
}

/************ LOOP ************/
void loop() {
  uint32_t now = millis();
  handleEncoderInput(gState);
  handleButtonInput(gState);
  updateStateMachine(gState, now);
}

/************ STATE + INPUT ************/
void handleEncoderInput(PomodoroState &st) {
  uint32_t now = millis();

  int8_t steps;
  noInterrupts();
  steps = gEncoder.steps;
  bool throttleExpired = (st.lastEncoderMs == 0) || (now - st.lastEncoderMs >= ENCODER_THROTTLE_MS);
  if (steps != 0 && throttleExpired) {
    gEncoder.steps = 0;
  } else {
    steps = 0;
  }
  interrupts();

  if (steps == 0) {
    return;
  }

  st.lastEncoderMs = now;

  int8_t dir = (steps > 0) ? 1 : -1;
  if (dir > 0) {
    st.optionIndex = (st.optionIndex + 1) % OPTION_COUNT;
  } else {
    st.optionIndex = (st.optionIndex + OPTION_COUNT - 1) % OPTION_COUNT;
  }

  enterSetting(st);
}

void handleButtonInput(PomodoroState &st) {
  btnDebounce.update();
  if (!btnDebounce.fell()) {
    return;
  }

  switch (st.mode) {
    case Mode::RUNNING:
      enterPaused(st);
      break;
    case Mode::PAUSED:
      resumeRun(st);
      break;
    case Mode::TIMEOUT:
      enterSetting(st);
      break;
    default:
      break;
  }
}

void updateStateMachine(PomodoroState &st, uint32_t now) {
  switch (st.mode) {
    case Mode::SETTING:
      if (!gDisplay.isAwake) {
        tftExitSleepSeqSoftOnly();
      }
      if (st.settingTween.isActive()) {
        renderAll(st, false, now);
        showCenterText(String(currentMinutes(st)), 4);
      }
      if (now - st.lastInputMs >= PREROLL_DELAY_MS) {
        enterPreRollShow(st);
      }
      break;
    case Mode::PREROLL_SHOW:
      if (now - st.stateTs >= PREROLL_HIDE_MS) {
        enterPreRollHide(st);
      }
      break;
    case Mode::PREROLL_HIDE:
      if (now - st.stateTs >= PREROLL_HIDE_MS) {
        startRunFromSelection(st);
      }
      break;
    case Mode::RUNNING: {
      if (st.runDurationMs == 0) {
        enterTimeout(st);
        break;
      }

      uint32_t elapsed = computeElapsedMs(st, now);
      if (elapsed >= st.runDurationMs) {
        enterTimeout(st);
      } else if (now - st.blinkTs >= RUN_REPAINT_MS) {
        st.blinkTs = now;
        renderAll(st, false, now);
      }
      break;
    }
    case Mode::PAUSED:
      if (now - st.pausedAtMs >= PAUSE_SLEEP_DELAY_MS) {
        goToSleep(st);
      } else if (now - st.blinkTs >= PAUSE_BLINK_MS) {
        st.blinkTs = now;
        st.blinkOn = !st.blinkOn;
        renderAll(st, false, st.pausedAtMs);
      }
      break;
    case Mode::TIMEOUT:
    case Mode::SLEEPING:
      break;
  }
}

/************ STATE ENTER HELPERS ************/
void enterSetting(PomodoroState &st) {
  uint32_t now = millis();
  bool wasSetting = (st.mode == Mode::SETTING);

  if (wasSetting) {
    st.settingFracCurrent = st.settingTween.sample(now);
  }

  st.mode = Mode::SETTING;
  st.stateTs = now;
  st.lastInputMs = now;
  resetBlink(st, now);

  float minutes = static_cast<float>(currentMinutes(st));
  float seconds = (minutes == 0.0f) ? 60.0f : minutes * 60.0f;
  float targetFrac = clampf(seconds / (60.0f * 60.0f), 0.0f, 1.0f);
  st.settingFracTarget = targetFrac;

  if (!wasSetting) {
    st.settingFracCurrent = targetFrac;
    st.settingTween.snapTo(targetFrac);
  } else {
    float current = st.settingFracCurrent;
    if (fabsf(targetFrac - current) <= SETTING_ANIM_EPSILON) {
      st.settingFracCurrent = targetFrac;
      st.settingTween.snapTo(targetFrac);
    } else {
      st.settingTween.startTween(current, targetFrac, now, SETTING_ANIM_DURATION_MS, easeOut);
    }
  }

  renderAll(st, !wasSetting, now);
  showCenterText(String(currentMinutes(st)), 4);
}

void enterPreRollShow(PomodoroState &st) {
  st.mode = Mode::PREROLL_SHOW;
  st.stateTs = millis();
  resetBlink(st, st.stateTs);
  st.settingTween.snapTo(st.settingFracTarget);
  st.settingFracCurrent = st.settingFracTarget;
  renderAll(st, true, st.stateTs);

  uint8_t val = currentMinutes(st);
  uint8_t showVal = (val == 0) ? 0 : (val - 1);
  showCenterText(String(showVal), 4);
}

void enterPreRollHide(PomodoroState &st) {
  st.mode = Mode::PREROLL_HIDE;
  st.stateTs = millis();
  resetBlink(st, st.stateTs);
  st.settingTween.snapTo(st.settingFracTarget);
  st.settingFracCurrent = st.settingFracTarget;
  renderAll(st, true, st.stateTs);
}

void startRunFromSelection(PomodoroState &st) {
  uint8_t minutes = currentMinutes(st);
  if (minutes == 0) {
    enterTimeout(st);
    return;
  }

  st.mode = Mode::RUNNING;
  st.stateTs = millis();
  st.runDurationMs = static_cast<uint32_t>(minutes) * 60UL * 1000UL;
  st.runStartMs = st.stateTs;
  st.pausedAtMs = 0;
  resetBlink(st, st.stateTs);
  renderAll(st, true, st.stateTs);
}

void resumeRun(PomodoroState &st) {
  if (st.mode != Mode::PAUSED) {
    return;
  }

  uint32_t now = millis();
  st.runStartMs += (now - st.pausedAtMs);
  st.pausedAtMs = 0;
  st.mode = Mode::RUNNING;
  st.stateTs = now;
  resetBlink(st, now);
  renderAll(st, true, now);
}

void enterPaused(PomodoroState &st) {
  st.mode = Mode::PAUSED;
  st.pausedAtMs = millis();
  st.stateTs = st.pausedAtMs;
  st.blinkTs = st.pausedAtMs;
  st.blinkOn = true;
  renderAll(st, true, st.pausedAtMs);
}

void enterTimeout(PomodoroState &st) {
  st.mode = Mode::TIMEOUT;
  for (uint8_t i = 0; i < TIMEOUT_BLINK_COUNT; ++i) {
    renderAll(st, true);
    showCenterText("0", 5);
    delay(250);
    renderAll(st, true);
    delay(250);
  }
  goToSleep(st);
}

void goToSleep(PomodoroState &st) {
  st.mode = Mode::SLEEPING;
  tft.fillScreen(COL_DARK);
  tftEnterSleepSeqSoftOnly();
  delay(50);
  configureLightSleepWakeup();
  esp_err_t err = esp_light_sleep_start();
  if (err != ESP_OK) {
    Serial.print("Light sleep failed: ");
    Serial.println(err);
  }
  tftExitSleepSeqSoftOnly();
  enterSetting(st);
}

/************ RENDERING ************/
void renderAll(PomodoroState &st, bool forceBg, uint32_t now) {

  if (now == UINT32_MAX) {
    now = millis();
  }

  drawDialBackground(forceBg);

  switch (st.mode) {
    case Mode::SETTING:
    case Mode::PREROLL_SHOW:
    case Mode::PREROLL_HIDE: {
      float minutes = static_cast<float>(currentMinutes(st));
      float totalSeconds = (minutes == 0.0f) ? 60.0f : minutes * 60.0f;
      float frac = st.settingFracTarget;
      if (st.mode == Mode::SETTING) {
        frac = st.settingTween.sample(now);
      }
      frac = clampf(frac, 0.0f, 1.0f);
      st.settingFracCurrent = frac;
      float remainingSeconds = frac * (60.0f * 60.0f);
      drawRemainingWedge(remainingSeconds, totalSeconds, false);
      drawMinuteHand(remainingSeconds, totalSeconds);
      break;
    }
    case Mode::RUNNING:
    case Mode::PAUSED: {
      if (st.runDurationMs == 0) {
        break;
      }

      uint32_t effectiveNow = (st.mode == Mode::PAUSED) ? st.pausedAtMs : now;
      float total = static_cast<float>(st.runDurationMs) / 1000.0f;
      float remaining = static_cast<float>(computeRemainingMs(st, effectiveNow)) / 1000.0f;

      uint32_t elapsed = computeElapsedMs(st, now);
      if (elapsed < 100) {
        drawRemainingWedge(remaining, total, st.mode == Mode::PAUSED);
      }
      drawMinuteHand(remaining, total);

      if (st.mode == Mode::PAUSED) {
        drawBlinkingTip(remaining, total, st.blinkOn);
        uint32_t remainingMs = computeRemainingMs(st, effectiveNow);
        uint32_t remainingMin = (remainingMs + 59999UL) / 60000UL;
        showCenterText(String(remainingMin), 4);
      }
      break;
    }
    case Mode::TIMEOUT:
    case Mode::SLEEPING:
      break;
  }
}

void drawDialBackground(bool clearAll) {
  if (clearAll) {
    tft.fillScreen(COL_BG);
  }
}

void drawRemainingWedge(float remainingSec, float totalSec, bool paused) {
  if (totalSec <= 0.0f) {
    return;
  }

  float frac = clampf(remainingSec / (60 * 60.1f), 0.0f, 1.0f);
  float sweep = 360.0f * frac;
  uint16_t col = paused ? COL_LIGHTRED : COL_RED;

  float startDeg = 0.0f;
  float endDeg = startDeg + sweep;

  // 배경 정리 후 새 호를 그려 잔상 제거
  fillSector(tft, CX, CY, R_OUT - 6, 0.0f, 360.0f, COL_BG, 4.0f);
  fillArc(tft, CX, CY, R_OUT - 6 - 2, R_OUT - 6, 0.0f, 360.0f, COL_BG, 4.0f);

  fillSector(tft, CX, CY, R_OUT - 6, startDeg, endDeg, col, 1.0f);
  // 빨간영역 호 그림자
  fillArc(tft, CX, CY, R_OUT - 6 - 2, R_OUT - 6, startDeg, endDeg, COL_RED_DARK, 1.0f);
}

void drawMinuteHand(float remainingSec, float totalSec) {
  if (totalSec <= 0.0f) {
    return;
  }


  float frac = clampf(remainingSec / (60 * 60.1f), 0.0f, 1.0f);
  float angle = 360.0f * frac;

  int16_t shadowX = CX + static_cast<int16_t>(cos(deg2rad(angle - 90 - 1)) * (R_OUT - 6));
  int16_t shadowY = CY + static_cast<int16_t>(sin(deg2rad(angle - 90 - 1)) * (R_OUT - 6));

  // 분침 그림자
  drawThickLine(tft, CX, CY, shadowX, shadowY, COL_RED_DARK, 3);

  // 이전거 지우기
  fillSector(tft, CX, CY, R_OUT - 6, angle, angle + 10, COL_BG, 1.0f);

  tft.fillCircle(CX, CY, 6, COL_RED);
}

// Draw a thicker line by drawing multiple parallel 1-pixel lines.
// This is a simple and fast way to emulate thickness on Adafruit_GFX.
void drawThickLine(Adafruit_GFX &gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color, uint8_t thickness) {
  if (thickness <= 1) {
    gfx.drawLine(x0, y0, x1, y1, color);
    return;
  }

  float dx = (float)(x1 - x0);
  float dy = (float)(y1 - y0);
  float len = sqrtf(dx*dx + dy*dy);
  if (len <= 0.001f) {
    gfx.fillCircle(x0, y0, thickness/2, color);
    return;
  }

  // unit perpendicular vector
  float px = -dy / len;
  float py = dx / len;

  int half = thickness / 2;
  for (int i = -half; i <= half; ++i) {
    int16_t ox = (int16_t)lrintf(px * i);
    int16_t oy = (int16_t)lrintf(py * i);
    gfx.drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
  }
}


void drawBlinkingTip(float remainingSec, float totalSec, bool on) {
  if (!on || totalSec <= 0.0f) {
    return;
  }

  float frac = clampf(remainingSec / totalSec, 0.0f, 1.0f);
  float angle = 270.0f + 360.0f * frac;
  float rad = deg2rad(angle);
  int16_t x = CX + static_cast<int16_t>(cos(rad) * (R_OUT - 2));
  int16_t y = CY + static_cast<int16_t>(sin(rad) * (R_OUT - 2));
  tft.fillCircle(x, y, 5, COL_LIGHTRED);
}

void showCenterText(const String &s, uint8_t textSize, uint16_t color, uint16_t bg) {
  tft.setTextColor(color, bg);
  tft.setTextSize(textSize);
  tft.setTextWrap(false);
  drawCenterText(s);
  tft.print(s);
}

void showCenterText(const char *s, uint8_t textSize, uint16_t color, uint16_t bg) {
  showCenterText(String(s), textSize, color, bg);
}

void drawCenterText(const String &s) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  int16_t x = CX - static_cast<int16_t>(w) / 2;
  int16_t y = CY - static_cast<int16_t>(h) / 2;
  tft.fillCircle(CX, CY, (w > h ? w : h) / 1.2f + CENTER_CLEAR_PADDING, COL_BG);
  tft.setCursor(x, y);
}

float easeLinear(float t) {
  return clampf(t, 0.0f, 1.0f);
}

float cubicBezierValue(float t, float p0, float p1, float p2, float p3) {
  float u = 1.0f - t;
  return (u * u * u * p0) + (3.0f * u * u * t * p1) + (3.0f * u * t * t * p2) + (t * t * t * p3);
}

static float cubicBezierDerivative(float t, float p0, float p1, float p2, float p3) {
  float u = 1.0f - t;
  return 3.0f * u * u * (p1 - p0) + 6.0f * u * t * (p2 - p1) + 3.0f * t * t * (p3 - p2);
}

float cubicBezierEase(float x, float x1, float y1, float x2, float y2) {
  x = clampf(x, 0.0f, 1.0f);
  float u = x;

  for (uint8_t i = 0; i < 5; ++i) {
    float current = cubicBezierValue(u, 0.0f, x1, x2, 1.0f) - x;
    float deriv = cubicBezierDerivative(u, 0.0f, x1, x2, 1.0f);
    if (fabsf(current) < 1e-5f) {
      break;
    }
    if (fabsf(deriv) < 1e-5f) {
      // 도함수가 너무 작으면 뉴턴 방법을 중단
      break;
    }
    u -= current / deriv;
    u = clampf(u, 0.0f, 1.0f);
  }

  float solvedX = cubicBezierValue(u, 0.0f, x1, x2, 1.0f);
  if (fabsf(solvedX - x) > 1e-3f) {
    float lo = 0.0f;
    float hi = 1.0f;
    u = x;
    for (uint8_t i = 0; i < 6; ++i) {
      float mid = (lo + hi) * 0.5f;
      float midX = cubicBezierValue(mid, 0.0f, x1, x2, 1.0f);
      if (midX < x) {
        lo = mid;
      } else {
        hi = mid;
      }
      u = mid;
    }
  }

  return clampf(cubicBezierValue(u, 0.0f, y1, y2, 1.0f), 0.0f, 1.0f);
}

float easeIn(float t) {
  return cubicBezierEase(t, 0.42f, 0.0f, 1.0f, 1.0f);
}

float easeOut(float t) {
  return cubicBezierEase(t, 0.0f, 0.0f, 0.58f, 1.0f);
}

float easeInOut(float t) {
  return cubicBezierEase(t, 0.42f, 0.0f, 0.58f, 1.0f);
}

void fillArc(Adafruit_GFX& gfx,
             int16_t cx, int16_t cy,
             int16_t r_inner, int16_t r_outer,
             float a0_deg, float a1_deg,
             uint16_t color,
             float step_deg) {
  if (r_inner > r_outer) { int16_t t = r_inner; r_inner = r_outer; r_outer = t; }

  auto norm = [](float a){ while (a < 0) a += 360; while (a >= 360) a -= 360; return a; };
  a0_deg = norm(a0_deg); a1_deg = norm(a1_deg);
  float sweep = a1_deg - a0_deg; if (sweep < 0) sweep += 360;

  const float DEG2RAD = 0.017453292519943295f;
  auto pt = [&](float a_deg, int16_t r, int16_t& x, int16_t& y){
    // 화면 좌표: 0°=12시 → 수학각(0°=3시, 반시계)로 변환
    float th = (90.0f - a_deg) * DEG2RAD;
    x = cx + (int16_t)lrintf(r * cosf(th));
    y = cy - (int16_t)lrintf(r * sinf(th));
  };

  for (float d = 0; d < sweep; d += step_deg) {
    float a  = a0_deg + d;
    float an = a0_deg + fminf(d + step_deg, sweep);
    if (a  >= 360) a  -= 360;
    if (an >= 360) an -= 360;

    int16_t i0x,i0y,i1x,i1y, o0x,o0y,o1x,o1y;
    pt(a ,  r_inner, i0x,i0y);
    pt(an,  r_inner, i1x,i1y);
    pt(a ,  r_outer, o0x,o0y);
    pt(an,  r_outer, o1x,o1y);

    // 사각형 띠를 두 개의 삼각형으로 채움 → 빈틈 없이 빠름
    gfx.fillTriangle(i0x,i0y, i1x,i1y, o0x,o0y, color);
    gfx.fillTriangle(o0x,o0y, i1x,i1y, o1x,o1y, color);
  }
}

void fillSector(Adafruit_GFX& gfx,
                int16_t cx, int16_t cy, int16_t r,
                float a0_deg, float a1_deg,
                uint16_t color,
                float step_deg) {
  auto norm = [](float a){ while(a < 0) a += 360; while(a >= 360) a -= 360; return a; };
  a0_deg = norm(a0_deg); a1_deg = norm(a1_deg);
  float sweep = a1_deg - a0_deg; if (sweep < 0) sweep += 360;

  const float DEG2RAD = 0.017453292519943295f;
  auto pt = [&](float a_deg, int16_t& x, int16_t& y){
    // 화면 좌표 변환: 0°=12시 → 수학각(0°=3시, 반시계)
    float th = (90.0f - a_deg) * DEG2RAD;
    x = cx + (int16_t)lrintf(r * cosf(th));
    y = cy - (int16_t)lrintf(r * sinf(th)); // 화면 Y는 아래로 증가
  };

  for (float d = 0; d < sweep; d += step_deg) {
    float a  = a0_deg + d;
    float an = a0_deg + fminf(d + step_deg, sweep);
    if (a  >= 360) a  -= 360;
    if (an >= 360) an -= 360;

    int16_t x0,y0,x1,y1;
    pt(a,  x0,y0);
    pt(an, x1,y1);
    // 중심-호-호 를 삼각형으로 채움 → 빠르고 빈틈 없음
    gfx.fillTriangle(cx, cy, x0, y0, x1, y1, color);
  }
}

/************ UTILS ************/
uint8_t currentMinutes(const PomodoroState &st) {
  return OPTIONS[st.optionIndex % OPTION_COUNT];
}

uint32_t computeElapsedMs(const PomodoroState &st, uint32_t now) {
  return now - st.runStartMs;
}

uint32_t computeRemainingMs(const PomodoroState &st, uint32_t now) {
  if (st.runDurationMs == 0) {
    return 0;
  }
  uint32_t elapsed = computeElapsedMs(st, now);
  return (elapsed >= st.runDurationMs) ? 0 : (st.runDurationMs - elapsed);
}

static bool enableRtcWakePin(uint8_t pin) {
  gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  if (!rtc_gpio_is_valid_gpio(gpio)) {
    return false;
  }
  rtc_gpio_pullup_en(gpio);
  rtc_gpio_pulldown_dis(gpio);
  return gpio_wakeup_enable(gpio, GPIO_INTR_LOW_LEVEL) == ESP_OK;
}

void configureLightSleepWakeup() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

  bool anyConfigured = false;
  anyConfigured |= enableRtcWakePin(ENC_BTN);
  anyConfigured |= enableRtcWakePin(ENC_A);
  anyConfigured |= enableRtcWakePin(ENC_B);

  if (anyConfigured) {
    esp_err_t err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK) {
      Serial.print("Failed to enable GPIO wakeup: ");
      Serial.println(err);
    }
  } else {
    Serial.println("No RTC-capable pins available for GPIO wakeup.");
  }
}

static inline void tftEnterSleepSeqSoftOnly(){
  gDisplay.isAwake = false;
  tft.startWrite(); tft.sendCommand(0x28); tft.endWrite(); // DISPOFF
  delay(10);
  tft.startWrite(); tft.sendCommand(0x10); tft.endWrite(); // SLPIN
  delay(120);
}


static inline void tftExitSleepSeqSoftOnly(){
  gDisplay.isAwake = true;
  tft.startWrite(); tft.sendCommand(0x11); tft.endWrite(); // SLPOUT
  delay(120);
  tft.startWrite(); tft.sendCommand(0x29); tft.endWrite(); // DISPON
}

 /************ FAST DIGITAL READ (compat) ************/
static inline uint8_t digitalReadFast(uint8_t pin) { return digitalRead(pin); }
