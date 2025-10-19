#include "pomodoro.h"

namespace {

uint8_t gPrevEncoderBits = 0;
int8_t gQuarterTurns = 0;
int8_t gPendingSteps = 0;

void pollEncoder() {
  uint8_t a = digitalReadFast(ENC_A);
  uint8_t b = digitalReadFast(ENC_B);
  uint8_t encoded = static_cast<uint8_t>((a << 1) | b);

  gPrevEncoderBits = static_cast<uint8_t>(((gPrevEncoderBits << 2) | encoded) & 0x0F);
  static const int8_t LUT[16] = { 0, -1, 1, 0,
                                   1,  0, 0, -1,
                                  -1,  0, 0,  1,
                                   0,  1, -1, 0 };

  int8_t delta = LUT[gPrevEncoderBits];
  if (!delta) {
    return;
  }

  gQuarterTurns = static_cast<int8_t>(gQuarterTurns + delta);
  if (gQuarterTurns >= 4) {
    gQuarterTurns = 0;
    ++gPendingSteps;
  } else if (gQuarterTurns <= -4) {
    gQuarterTurns = 0;
    --gPendingSteps;
  }
}

}  // namespace

void handleEncoderInput(PomodoroState &st) {
  pollEncoder();

  uint32_t now = millis();
  bool throttleExpired = (st.lastEncoderMs == 0) || (now - st.lastEncoderMs >= ENCODER_THROTTLE_MS);

  int8_t steps = gPendingSteps;
  gPendingSteps = 0;

  if (!throttleExpired) {
    steps = 0;
  }

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

  enterSetting(st, true);
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

