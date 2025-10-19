/*
  ESP32-S3 Pomodoro Timer on 1.28" Round TFT (240x240, GC9A01A)
  - Rotary encoder: cycles minutes CW 15→30→60→0→15 (CCW reversed)
  - Click to pause/resume
  - UI: gray background, remaining time as red wedge + minute hand
  - While paused: lighter red, arc tip blinks, remaining minutes shown at center
  - Idle after pause 3 min → 설정 화면으로 복귀
  - Timeout (0) → blink "0" 5x → 설정 화면으로 복귀
  - Setting flow: rotating immediately restarts the timer, shows minutes for 2s, and draws the wedge with a 0.3s ease-out

  Board: ESP32-S3 (3.3V)
  Libraries (Library Manager):
    - Adafruit GFX Library
    - Adafruit GC9A01A (a.k.a. Adafruit_GC9A01A)
    - Bounce2
  - No additional low-power library required

  Pin mapping below can be edited to your wiring.
*/

#include "src/pomodoro.h"

Adafruit_GC9A01A tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
PomodoroState gState;
DisplayState gDisplay;
Bounce btnDebounce;

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

}

void loop() {
  uint32_t now = millis();
  handleEncoderInput(gState);
  handleButtonInput(gState);
  updateStateMachine(gState, now);
}

