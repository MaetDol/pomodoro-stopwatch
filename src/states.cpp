#include "pomodoro.h"

namespace {

void clearCenterDisplay(PomodoroState &st) {
  st.centerDisplayUntilMs = 0;
  st.centerDisplayValue = 0;
  st.pendingTimeout = false;
  resetCenterFade(gDisplay);
}

void startRunForMinutes(PomodoroState &st, uint8_t minutes, uint32_t now) {
  st.runDurationMs = static_cast<uint32_t>(minutes) * 60UL * 1000UL;
  st.runStartMs = now;
  st.pausedAtMs = 0;
  resetBlink(st, now);
}

}  // namespace

void enterSetting(PomodoroState &st, bool preserveDial) {
  uint32_t now = millis();

  st.mode = Mode::SETTING;
  st.stateTs = now;
  st.lastInputMs = now;
  st.lastEncoderMs = now;
  resetBlink(st, now);
  resetCenterFill(gDisplay);

  uint8_t minutes = currentMinutes(st);
  float seconds = static_cast<float>(minutes) * 60.0f;
  float targetFrac = clampf(seconds / (60.0f * 60.0f), 0.0f, 1.0f);

  float startFrac = preserveDial ? clampf(st.settingFracCurrent, 0.0f, 1.0f) : 0.0f;
  st.settingFracTarget = targetFrac;
  st.settingFracCurrent = startFrac;
  gDisplay.animations.start(&st.settingFracCurrent,
                             startFrac,
                             targetFrac,
                             now,
                             SETTING_ANIM_DURATION_MS,
                             easeOut);

  if (minutes == 0) {
    gDisplay.animations.remove(&st.settingFracCurrent);
    st.settingFracCurrent = targetFrac;
  }

  st.centerDisplayValue = minutes;
  st.centerDisplayUntilMs = now + CENTER_DISPLAY_MS;
  st.pendingTimeout = (minutes == 0);

  if (!st.pendingTimeout) {
    startRunForMinutes(st, minutes, now);
  } else {
    st.runDurationMs = 0;
    st.runStartMs = now;
    st.pausedAtMs = 0;
  }

  renderAll(st, true, now);
}

void resumeRun(PomodoroState &st) {
  if (st.mode != Mode::PAUSED) {
    return;
  }

  uint32_t now = millis();
  uint32_t pausedAt = st.pausedAtMs;
  if (pausedAt != 0) {
    uint32_t pausedDuration = (now >= pausedAt) ? (now - pausedAt)
                                               : (UINT32_MAX - pausedAt + 1U + now);
    st.runStartMs += pausedDuration;
  } else {
    st.runStartMs = now;
  }
  st.pausedAtMs = 0;
  st.lastInputMs = now;
  st.mode = Mode::RUNNING;
  st.stateTs = now;
  clearCenterDisplay(st);
  resetBlink(st, now);
  renderAll(st, true, now);
}

void enterPaused(PomodoroState &st) {
  clearCenterDisplay(st);
  st.mode = Mode::PAUSED;
  st.pausedAtMs = millis();
  st.stateTs = st.pausedAtMs;
  st.blinkTs = st.pausedAtMs;
  st.blinkOn = true;
  st.blinkDurationMs = PAUSE_BLINK_WHITE_MS;
  st.blinkFrameTs = st.pausedAtMs;
  st.blinkLevel = 0.0f;
  gDisplay.animations.start(&st.blinkLevel,
                                 st.blinkLevel,
                                 1.0f,
                                 st.pausedAtMs,
                                 st.blinkDurationMs,
                                 easeOut);
  renderAll(st, true, st.pausedAtMs);
}

namespace {

bool waitForEncoderDuringTimeout(PomodoroState &st, uint32_t durationMs) {
  uint32_t start = millis();
  while (millis() - start < durationMs) {
    handleEncoderInput(st);
    if (st.mode == Mode::SETTING) {
      return true;
    }
    delay(1);
  }
  return false;
}

}  // namespace

void enterTimeout(PomodoroState &st) {
  clearCenterDisplay(st);
  st.mode = Mode::TIMEOUT;
  for (uint8_t i = 0; i < TIMEOUT_BLINK_COUNT; ++i) {
    renderAll(st, true);
    showCenterText("0", 5);
    if (waitForEncoderDuringTimeout(st, 250)) {
      return;
    }
    renderAll(st, true);
    if (waitForEncoderDuringTimeout(st, 250)) {
      return;
    }
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
  clearCenterDisplay(st);
  enterSetting(st);
}

