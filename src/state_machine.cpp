#include "pomodoro.h"

void updateStateMachine(PomodoroState &st, uint32_t now) {
  switch (st.mode) {
    case Mode::SETTING:
      if (!gDisplay.isAwake) {
        tftExitSleepSeqSoftOnly();
      }
      if (st.settingTween.isActive()) {
        renderAll(st, false, now);
        showCenterText(String(currentMinutes(st)), 4);
      }
      if (now - st.lastInputMs >= PREROLL_DELAY_MS) {
        enterPreRollShow(st);
      }
      break;
    case Mode::PREROLL_SHOW:
      if (now - st.stateTs >= PREROLL_HIDE_MS) {
        enterPreRollHide(st);
      }
      break;
    case Mode::PREROLL_HIDE:
      if (now - st.stateTs >= PREROLL_HIDE_MS) {
        startRunFromSelection(st);
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
    case Mode::PAUSED:
      if (now - st.pausedAtMs >= PAUSE_SLEEP_DELAY_MS) {
        goToSleep(st);
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

