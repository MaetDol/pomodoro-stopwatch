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
    case Mode::PAUSED: {
      if (now - st.pausedAtMs >= PAUSE_SLEEP_DELAY_MS) {
        enterTimeout(st);
        break;
      }

      bool needsRender = false;
      uint32_t duration = st.blinkDurationMs > 0 ? st.blinkDurationMs
                                                : (st.blinkOn ? PAUSE_BLINK_WHITE_MS
                                                              : PAUSE_BLINK_BLACK_MS);
      if (now - st.blinkTs >= duration) {
        st.blinkTs = now;
        st.blinkFromLevel = st.blinkToLevel;
        st.blinkOn = !st.blinkOn;
        st.blinkToLevel = st.blinkOn ? 1.0f : 0.0f;
        st.blinkDurationMs = st.blinkOn ? PAUSE_BLINK_WHITE_MS : PAUSE_BLINK_BLACK_MS;
        st.blinkFrameTs = now;
        needsRender = true;
      }

      if (now - st.blinkFrameTs >= PAUSE_BLINK_FRAME_MS) {
        st.blinkFrameTs = now;
        needsRender = true;
      }

      if (needsRender) {
        renderAll(st, false, now);
      }
      break;
    }
    case Mode::TIMEOUT:
    case Mode::SLEEPING:
      break;
  }
}

