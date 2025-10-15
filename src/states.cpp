#include "pomodoro.h"

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
  enterSetting(st);
}

