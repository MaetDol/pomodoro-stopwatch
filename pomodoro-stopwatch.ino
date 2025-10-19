/*
  ESP32-S3 Pomodoro Timer on 1.28" Round TFT (240x240, GC9A01A)
  - Rotary encoder: cycles minutes CW 15→30→60→0→15 (CCW reversed)
  - Click to pause/resume
  - UI: gray background, remaining time as red wedge + minute hand
  - While paused: lighter red, arc tip blinks, remaining minutes shown at center
  - Idle after pause 3 min → sleep
  - Timeout (0) → blink "0" 5x → sleep
  - Setting flow: rotating immediately restarts the timer, shows minutes for 2s, and draws the wedge with a 0.3s ease-out

  Board: ESP32-S3 (3.3V)
  Libraries (Library Manager):
    - Adafruit GFX Library
    - Adafruit GC9A01A (a.k.a. Adafruit_GC9A01A)
    - Bounce2
  - No additional low-power library required (uses native ESP32 light sleep API)

  Pin mapping below can be edited to your wiring.
*/

#include "src/pomodoro.h"

Adafruit_GC9A01A tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
EncoderState gEncoder;
PomodoroState gState;
DisplayState gDisplay;
Bounce btnDebounce;

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
  SPI.setFrequency(40000000); 

  tft.begin();
  tft.setSPISpeed(40000000);

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

void loop() {
  handleEncoderInput(gState);
  handleButtonInput(gState);
  uint32_t now = millis();
  updateStateMachine(gState, now);
}

