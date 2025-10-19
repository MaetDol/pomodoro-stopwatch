#include "pomodoro.h"

void updateStateMachine(PomodoroState &st, uint32_t now) {
  gDisplay.animations.updateAll(now);

  switch (st.mode) {
    case Mode::SETTING:
      if (st.pendingTimeout) {
        enterTimeout(st);
        break;
      }

      renderAll(st, false, now);
      if (now > st.centerDisplayUntilMs) {
        st.centerDisplayUntilMs = 0;
        st.centerDisplayValue = 0;
        st.pendingTimeout = false;
        st.stateTs = now;
        st.blinkTs = now;
        gDisplay.animations.remove(&st.settingFracCurrent);
        st.mode = Mode::RUNNING;
        resetDisplayCache(gDisplay);
        renderAll(st, true, now);
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
      } else if (now - st.blinkTs >= RUN_REPAINT_MS) {
        st.blinkTs = now;
        renderAll(st, false, now);
      }
      break;
    }
    case Mode::PAUSED: {
      if (now - st.pausedAtMs >= PAUSE_IDLE_DELAY_MS) {
        enterTimeout(st);
        break;
      }

      bool needsRender = false;
      uint32_t duration = st.blinkDurationMs > 0 ? st.blinkDurationMs
                                                : (st.blinkOn ? PAUSE_BLINK_WHITE_MS
                                                              : PAUSE_BLINK_BLACK_MS);
      bool animActive = gDisplay.animations.isActive(&st.blinkLevel);
      if (!animActive && now - st.blinkTs >= duration) {
        st.blinkTs = now;
        st.blinkOn = !st.blinkOn;
        st.blinkDurationMs = st.blinkOn ? PAUSE_BLINK_WHITE_MS : PAUSE_BLINK_BLACK_MS;
        st.blinkFrameTs = now;
        gDisplay.animations.start(&st.blinkLevel,
                                       st.blinkLevel,
                                       st.blinkOn ? 1.0f : 0.0f,
                                       now,
                                       st.blinkDurationMs,
                                       easeOut);
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
      break;
  }
}

