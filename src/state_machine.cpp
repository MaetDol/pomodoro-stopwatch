#include "pomodoro.h"

void updateStateMachine(PomodoroState &st, uint32_t now) {
  switch (st.mode) {
    case Mode::SETTING:
      if (!gDisplay.isAwake) {
        tftExitSleepSeqSoftOnly();
      }
      if (st.settingTween.isActive()) {
        renderAll(st, false, now);
      }
      if (now < st.centerDisplayUntilMs) {
        showCenterText(String(st.centerDisplayValue), 4);
        break;
      }

      if (st.pendingTimeout) {
        enterTimeout(st);
        break;
      }

      st.centerDisplayUntilMs = 0;
      st.centerDisplayValue = 0;
      st.pendingTimeout = false;
      st.mode = Mode::RUNNING;
      st.stateTs = now;
      st.blinkTs = now;
      renderAll(st, true, now);
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
        enterTimeout(st);
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

