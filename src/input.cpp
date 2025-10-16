#include "pomodoro.h"

void handleEncoderInput(PomodoroState &st) {
  uint32_t now = millis();

  int8_t steps;
  noInterrupts();
  steps = gEncoder.steps;
  bool throttleExpired = (st.lastEncoderMs == 0) || (now - st.lastEncoderMs >= ENCODER_THROTTLE_MS);
  gEncoder.steps = 0;
  if (!throttleExpired) {
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
    case Mode::SETTING:
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

