#include "pomodoro.h"

void renderAll(PomodoroState &st, bool forceBg, uint32_t now) {
  if (now == UINT32_MAX) {
    now = millis();
  }

  drawDialBackground(forceBg);

  switch (st.mode) {
    case Mode::SETTING:
    case Mode::PREROLL_SHOW:
    case Mode::PREROLL_HIDE: {
      float minutes = static_cast<float>(currentMinutes(st));
      float totalSeconds = (minutes == 0.0f) ? 60.0f : minutes * 60.0f;
      float frac = st.settingFracTarget;
      if (st.mode == Mode::SETTING) {
        frac = st.settingTween.sample(now);
      }
      frac = clampf(frac, 0.0f, 1.0f);
      st.settingFracCurrent = frac;
      float remainingSeconds = frac * (60.0f * 60.0f);
      drawRemainingWedge(remainingSeconds, totalSeconds, false);
      drawMinuteHand(remainingSeconds, totalSeconds);
      break;
    }
    case Mode::RUNNING:
    case Mode::PAUSED: {
      if (st.runDurationMs == 0) {
        break;
      }

      uint32_t effectiveNow = (st.mode == Mode::PAUSED) ? st.pausedAtMs : now;
      float total = static_cast<float>(st.runDurationMs) / 1000.0f;
      float remaining = static_cast<float>(computeRemainingMs(st, effectiveNow)) / 1000.0f;

      uint32_t elapsed = computeElapsedMs(st, now);
      if (elapsed < 100) {
        drawRemainingWedge(remaining, total, st.mode == Mode::PAUSED);
      }
      drawMinuteHand(remaining, total);

      if (st.mode == Mode::PAUSED) {
        drawBlinkingTip(remaining, total, st.blinkOn);
        uint32_t remainingMs = computeRemainingMs(st, effectiveNow);
        uint32_t remainingMin = (remainingMs + 59999UL) / 60000UL;
        showCenterText(String(remainingMin), 4);
      }
      break;
    }
    case Mode::TIMEOUT:
    case Mode::SLEEPING:
      break;
  }
}

void drawDialBackground(bool clearAll) {
  if (clearAll) {
    tft.fillScreen(COL_BG);
  }
}

void drawRemainingWedge(float remainingSec, float totalSec, bool paused) {
  if (totalSec <= 0.0f) {
    return;
  }

  float frac = clampf(remainingSec / (60 * 60.1f), 0.0f, 1.0f);
  float sweep = 360.0f * frac;
  uint16_t col = paused ? COL_LIGHTRED : COL_RED;

  float startDeg = 0.0f;
  float endDeg = startDeg + sweep;

  // 배경 정리 후 새 호를 그려 잔상 제거
  fillSector(tft, CX, CY, R_OUT - 6, 0.0f, 360.0f, COL_BG, 4.0f);
  fillArc(tft, CX, CY, R_OUT - 6 - 2, R_OUT - 6, 0.0f, 360.0f, COL_BG, 4.0f);

  fillSector(tft, CX, CY, R_OUT - 6, startDeg, endDeg, col, 1.0f);
  // 빨간영역 호 그림자
  fillArc(tft, CX, CY, R_OUT - 6 - 2, R_OUT - 6, startDeg, endDeg, COL_RED_DARK, 1.0f);
}

void drawMinuteHand(float remainingSec, float totalSec) {
  if (totalSec <= 0.0f) {
    return;
  }

  float frac = clampf(remainingSec / (60 * 60.1f), 0.0f, 1.0f);
  float angle = 360.0f * frac;

  int16_t shadowX = CX + static_cast<int16_t>(cos(deg2rad(angle - 90 - 1)) * (R_OUT - 6));
  int16_t shadowY = CY + static_cast<int16_t>(sin(deg2rad(angle - 90 - 1)) * (R_OUT - 6));

  // 분침 그림자
  drawThickLine(tft, CX, CY, shadowX, shadowY, COL_RED_DARK, 3);

  // 이전거 지우기
  fillSector(tft, CX, CY, R_OUT - 6, angle, angle + 10, COL_BG, 1.0f);

  tft.fillCircle(CX, CY, 6, COL_RED);
}

void drawThickLine(Adafruit_GFX &gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color, uint8_t thickness) {
  if (thickness <= 1) {
    gfx.drawLine(x0, y0, x1, y1, color);
    return;
  }

  float dx = (float)(x1 - x0);
  float dy = (float)(y1 - y0);
  float len = sqrtf(dx*dx + dy*dy);
  if (len <= 0.001f) {
    gfx.fillCircle(x0, y0, thickness/2, color);
    return;
  }

  // unit perpendicular vector
  float px = -dy / len;
  float py = dx / len;

  int half = thickness / 2;
  for (int i = -half; i <= half; ++i) {
    int16_t ox = (int16_t)lrintf(px * i);
    int16_t oy = (int16_t)lrintf(py * i);
    gfx.drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
  }
}

void drawBlinkingTip(float remainingSec, float totalSec, bool on) {
  if (!on || totalSec <= 0.0f) {
    return;
  }

  float frac = clampf(remainingSec / totalSec, 0.0f, 1.0f);
  float angle = 270.0f + 360.0f * frac;
  float rad = deg2rad(angle);
  int16_t x = CX + static_cast<int16_t>(cos(rad) * (R_OUT - 2));
  int16_t y = CY + static_cast<int16_t>(sin(rad) * (R_OUT - 2));
  tft.fillCircle(x, y, 5, COL_LIGHTRED);
}

void showCenterText(const String &s, uint8_t textSize, uint16_t color, uint16_t bg) {
  tft.setTextColor(color, bg);
  tft.setTextSize(textSize);
  tft.setTextWrap(false);
  drawCenterText(s);
  tft.print(s);
}

void showCenterText(const char *s, uint8_t textSize, uint16_t color, uint16_t bg) {
  showCenterText(String(s), textSize, color, bg);
}

void drawCenterText(const String &s) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  int16_t x = CX - static_cast<int16_t>(w) / 2;
  int16_t y = CY - static_cast<int16_t>(h) / 2;
  tft.fillCircle(CX, CY, (w > h ? w : h) / 1.2f + CENTER_CLEAR_PADDING, COL_BG);
  tft.setCursor(x, y);
}

void fillArc(Adafruit_GFX& gfx,
             int16_t cx, int16_t cy,
             int16_t r_inner, int16_t r_outer,
             float a0_deg, float a1_deg,
             uint16_t color,
             float step_deg) {
  if (r_inner > r_outer) { int16_t t = r_inner; r_inner = r_outer; r_outer = t; }

  auto norm = [](float a){ while (a < 0) a += 360; while (a >= 360) a -= 360; return a; };
  a0_deg = norm(a0_deg); a1_deg = norm(a1_deg);
  float sweep = a1_deg - a0_deg; if (sweep < 0) sweep += 360;

  const float DEG2RAD = 0.017453292519943295f;
  auto pt = [&](float a_deg, int16_t r, int16_t& x, int16_t& y){
    // 화면 좌표: 0°=12시 → 수학각(0°=3시, 반시계)로 변환
    float th = (90.0f - a_deg) * DEG2RAD;
    x = cx + (int16_t)lrintf(r * cosf(th));
    y = cy - (int16_t)lrintf(r * sinf(th));
  };

  for (float d = 0; d < sweep; d += step_deg) {
    float a  = a0_deg + d;
    float an = a0_deg + fminf(d + step_deg, sweep);
    if (a  >= 360) a  -= 360;
    if (an >= 360) an -= 360;

    int16_t i0x,i0y,i1x,i1y, o0x,o0y,o1x,o1y;
    pt(a ,  r_inner, i0x,i0y);
    pt(an,  r_inner, i1x,i1y);
    pt(a ,  r_outer, o0x,o0y);
    pt(an,  r_outer, o1x,o1y);

    // 사각형 띠를 두 개의 삼각형으로 채움 → 빈틈 없이 빠름
    gfx.fillTriangle(i0x,i0y, i1x,i1y, o0x,o0y, color);
    gfx.fillTriangle(o0x,o0y, i1x,i1y, o1x,o1y, color);
  }
}

void fillSector(Adafruit_GFX& gfx,
                int16_t cx, int16_t cy, int16_t r,
                float a0_deg, float a1_deg,
                uint16_t color,
                float step_deg) {
  auto norm = [](float a){ while(a < 0) a += 360; while(a >= 360) a -= 360; return a; };
  a0_deg = norm(a0_deg); a1_deg = norm(a1_deg);
  float sweep = a1_deg - a0_deg; if (sweep < 0) sweep += 360;

  const float DEG2RAD = 0.017453292519943295f;
  auto pt = [&](float a_deg, int16_t& x, int16_t& y){
    // 화면 좌표 변환: 0°=12시 → 수학각(0°=3시, 반시계)
    float th = (90.0f - a_deg) * DEG2RAD;
    x = cx + (int16_t)lrintf(r * cosf(th));
    y = cy - (int16_t)lrintf(r * sinf(th)); // 화면 Y는 아래로 증가
  };

  for (float d = 0; d < sweep; d += step_deg) {
    float a  = a0_deg + d;
    float an = a0_deg + fminf(d + step_deg, sweep);
    if (a  >= 360) a  -= 360;
    if (an >= 360) an -= 360;

    int16_t x0,y0,x1,y1;
    pt(a,  x0,y0);
    pt(an, x1,y1);
    // 중심-호-호 를 삼각형으로 채움 → 빠르고 빈틈 없음
    gfx.fillTriangle(cx, cy, x0, y0, x1, y1, color);
  }
}

