#include "pomodoro.h"

uint8_t currentMinutes(const PomodoroState &st) {
  return OPTIONS[st.optionIndex % OPTION_COUNT];
}

uint32_t computeElapsedMs(const PomodoroState &st, uint32_t now) {
  uint32_t start = st.runStartMs;
  return (now >= start) ? (now - start) : (UINT32_MAX - start + 1U + now);
}

uint32_t computeRemainingMs(const PomodoroState &st, uint32_t now) {
  if (st.runDurationMs == 0) {
    return 0;
  }
  uint32_t elapsed = computeElapsedMs(st, now);
  return (elapsed >= st.runDurationMs) ? 0 : (st.runDurationMs - elapsed);
}

