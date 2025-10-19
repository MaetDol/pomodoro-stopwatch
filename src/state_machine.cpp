#include "pomodoro.h"

void updateStateMachine(PomodoroState &st, uint32_t now) {
  switch (st.mode) {
    case Mode::SETTING:
      if (!gDisplay.isAwake) {
        tftExitSleepSeqSoftOnly();
      }

      if (st.pendingTimeout) {
        enterTimeout(st);
        break;
      }

      renderAll(st, false, now);
      if (st.centerDisplayUntilMs != 0 &&
          static_cast<int32_t>(now - st.centerDisplayUntilMs) >= 0) {
        st.centerDisplayUntilMs = 0;
        st.centerDisplayValue = 0;
        st.pendingTimeout = false;
        st.stateTs = now;
        st.blinkTs = now;
        st.mode = Mode::RUNNING;
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
      } else if (elapsedSince(st.blinkTs, now) >= RUN_REPAINT_MS) {
        st.blinkTs = now;
        renderAll(st, false, now);
      }
      break;
    }
    case Mode::PAUSED: {
      uint32_t pausedElapsed = (st.pausedAtMs == 0) ? 0 : elapsedSince(st.pausedAtMs, now);
      if (st.pausedAtMs != 0 && pausedElapsed >= PAUSE_SLEEP_DELAY_MS) {
        enterTimeout(st);
        break;
      }

      bool needsRender = false;
      uint32_t duration = st.blinkDurationMs > 0 ? st.blinkDurationMs
                                                : (st.blinkOn ? PAUSE_BLINK_WHITE_MS
                                                              : PAUSE_BLINK_BLACK_MS);
      if (elapsedSince(st.blinkTs, now) >= duration) {
        st.blinkTs = now;
        st.blinkFromLevel = st.blinkToLevel;
        st.blinkOn = !st.blinkOn;
        st.blinkToLevel = st.blinkOn ? 1.0f : 0.0f;
        st.blinkDurationMs = st.blinkOn ? PAUSE_BLINK_WHITE_MS : PAUSE_BLINK_BLACK_MS;
        st.blinkFrameTs = now;
        needsRender = true;
      }

      if (elapsedSince(st.blinkFrameTs, now) >= PAUSE_BLINK_FRAME_MS) {
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

