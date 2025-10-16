#include "pomodoro.h"

namespace {

constexpr float ANGLE_EPS = 0.25f;
constexpr float POINTER_RESTORE_SPAN = 2.5f;
constexpr int16_t WEDGE_RADIUS = R_OUT - 6;
constexpr int16_t ARC_INNER = WEDGE_RADIUS - 2;
constexpr int16_t ARC_OUTER = WEDGE_RADIUS;

uint16_t blend565(uint16_t from, uint16_t to, float t) {
  t = clampf(t, 0.0f, 1.0f);
  uint8_t fr = (from >> 11) & 0x1F;
  uint8_t fg = (from >> 5) & 0x3F;
  uint8_t fb = from & 0x1F;

  uint8_t tr = (to >> 11) & 0x1F;
  uint8_t tg = (to >> 5) & 0x3F;
  uint8_t tb = to & 0x1F;

  uint8_t rr = static_cast<uint8_t>(lrintf(lerpf(fr, tr, t)));
  uint8_t rg = static_cast<uint8_t>(lrintf(lerpf(fg, tg, t)));
  uint8_t rb = static_cast<uint8_t>(lrintf(lerpf(fb, tb, t)));

  return (rr << 11) | (rg << 5) | rb;
}

float normalizeAngle(float deg) {
  while (deg < 0.0f) { deg += 360.0f; }
  while (deg >= 360.0f) { deg -= 360.0f; }
  return deg;
}

float hourScaledFraction(float seconds) {
  return clampf(seconds / (60.0f * 60.1f), 0.0f, 1.0f);
}

void polarToScreen(float angleDeg, int16_t radius, int16_t &x, int16_t &y) {
  float theta = deg2rad(90.0f - normalizeAngle(angleDeg));
  x = CX + static_cast<int16_t>(lrintf(radius * cosf(theta)));
  y = CY - static_cast<int16_t>(lrintf(radius * sinf(theta)));
}

void paintRingSegment(float fromDeg, float toDeg, uint16_t fillColor, uint16_t arcColor, float step = 1.0f) {
  float sweep = toDeg - fromDeg;
  if (sweep < 0.0f) {
    sweep += 360.0f;
  }
  if (sweep <= ANGLE_EPS) {
    return;
  }

  float start = normalizeAngle(fromDeg);
  float target = start + sweep;
  fillSector(tft, CX, CY, WEDGE_RADIUS, start, target, fillColor, step);
  fillArc(tft, CX, CY, ARC_INNER, ARC_OUTER, start, target, arcColor, step);
}

uint16_t backgroundColorForMode(const PomodoroState &st) {
  switch (st.mode) {
    case Mode::PAUSED:
      return COL_BG_PAUSED;
    default:
      return COL_BG;
  }
}

void clearDialArea(uint16_t bgColor) {
  paintRingSegment(0.0f, 360.0f, bgColor, bgColor, 4.0f);
}

}  // namespace

void renderAll(PomodoroState &st, bool forceBg, uint32_t now) {
  if (now == UINT32_MAX) {
    now = millis();
  }

  if (forceBg) {
    resetDisplayCache(gDisplay);
  }

  uint16_t bgColor = backgroundColorForMode(st);

  drawDialBackground(bgColor, forceBg);

  switch (st.mode) {
    case Mode::SETTING: {
      float minutes = static_cast<float>(currentMinutes(st));
      float totalSeconds = (minutes == 0.0f) ? 60.0f : minutes * 60.0f;
      float frac = st.settingFracTarget;
      frac = st.settingTween.sample(now);
      frac = clampf(frac, 0.0f, 1.0f);
      st.settingFracCurrent = frac;
      float remainingSeconds = frac * (60.0f * 60.0f);
      drawRemainingWedge(remainingSeconds, totalSeconds, false, bgColor);
      drawMinuteHand(remainingSeconds, totalSeconds, bgColor, COL_RED_DARK, COL_RED);
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
        drawRemainingWedge(remaining, total, st.mode == Mode::PAUSED, bgColor);
      }
      if (st.mode == Mode::PAUSED) {
        uint32_t duration = st.blinkDurationMs ? st.blinkDurationMs
                                               : (st.blinkOn ? PAUSE_BLINK_WHITE_MS
                                                             : PAUSE_BLINK_BLACK_MS);
        uint32_t blinkElapsed = (now >= st.blinkTs) ? (now - st.blinkTs) : 0;
        float t = duration == 0 ? 1.0f
                                : clampf(static_cast<float>(blinkElapsed) / static_cast<float>(duration), 0.0f, 1.0f);
        float eased = easeOut(t);
        float blinkLevel = lerpf(st.blinkFromLevel, st.blinkToLevel, eased);
        uint16_t pointerColor = blend565(COL_BLACK, COL_WHITE, blinkLevel);
        drawMinuteHand(remaining, total, bgColor, pointerColor, pointerColor);
        drawBlinkingTip(remaining, total, blinkLevel >= 0.5f, bgColor);
        uint32_t remainingMs = computeRemainingMs(st, effectiveNow);
        uint32_t remainingMin = (remainingMs + 59999UL) / 60000UL;
        showCenterText(String(remainingMin), 4, COL_RED, bgColor);
      } else {
        drawMinuteHand(remaining, total, bgColor, COL_RED_DARK, COL_RED);
        drawBlinkingTip(remaining, total, false, bgColor);
      }
      break;
    }
    case Mode::TIMEOUT:
    case Mode::SLEEPING:
      break;
  }
}

void drawDialBackground(uint16_t bgColor, bool clearAll) {
  if (clearAll) {
    tft.fillScreen(bgColor);
  }
}

void drawRemainingWedge(float remainingSec, float totalSec, bool paused, uint16_t bgColor) {
  if (totalSec <= 0.0f) {
    return;
  }

  DisplayDialCache &cache = gDisplay.dial;
  cache.prevWedgeEndDeg = cache.wedgeEndDeg;

  float frac = hourScaledFraction(remainingSec);
  float newEnd = 360.0f * frac;
  uint16_t color = paused ? COL_LIGHTRED : COL_RED;
  bool colorChanged = !cache.wedgeValid || cache.wedgeColor != color;

  if (!cache.wedgeValid || colorChanged) {
    clearDialArea(bgColor);
    if (newEnd > ANGLE_EPS) {
      paintRingSegment(0.0f, newEnd, color, COL_RED_DARK);
    }
  } else {
    if (newEnd + ANGLE_EPS < cache.wedgeEndDeg) {
      paintRingSegment(newEnd, cache.wedgeEndDeg, bgColor, bgColor);
    } else if (newEnd > cache.wedgeEndDeg + ANGLE_EPS) {
      paintRingSegment(cache.wedgeEndDeg, newEnd, color, COL_RED_DARK);
    }
  }

  cache.wedgeValid = true;
  cache.wedgeEndDeg = newEnd;
  cache.wedgeColor = color;
}

void drawMinuteHand(float remainingSec, float totalSec, uint16_t bgColor, uint16_t pointerColor, uint16_t hubColor) {
  if (totalSec <= 0.0f) {
    return;
  }

  DisplayDialCache &cache = gDisplay.dial;
  float frac = hourScaledFraction(remainingSec);
  float angle = 360.0f * frac;

  if (cache.pointerValid) {
    float start = cache.pointerAngleDeg - POINTER_RESTORE_SPAN;
    if (cache.prevWedgeEndDeg > cache.wedgeEndDeg + ANGLE_EPS) {
      paintRingSegment(start, cache.pointerAngleDeg, bgColor, bgColor);
    } else {
      paintRingSegment(start, cache.pointerAngleDeg, cache.wedgeColor, COL_RED_DARK);
    }
  }

  int16_t shadowX, shadowY;
  polarToScreen(angle - 1.0f, WEDGE_RADIUS, shadowX, shadowY);
  drawThickLine(tft, CX, CY, shadowX, shadowY, pointerColor, 3);

  fillSector(tft, CX, CY, WEDGE_RADIUS, angle, angle + 10.0f, bgColor, 1.0f);
  tft.fillCircle(CX, CY, 6, hubColor);

  cache.pointerValid = true;
  cache.pointerAngleDeg = angle;
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

void drawBlinkingTip(float remainingSec, float totalSec, bool on, uint16_t bgColor) {
  DisplayDialCache &cache = gDisplay.dial;

  auto clearPrevious = [&]() {
    if (!cache.blinkVisible) {
      return;
    }
    uint16_t base = (cache.blinkAngleDeg <= cache.wedgeEndDeg + ANGLE_EPS)
                        ? cache.wedgeColor
                        : bgColor;
    tft.fillCircle(cache.blinkX, cache.blinkY, 5, base);
    cache.blinkVisible = false;
  };

  if (!on || totalSec <= 0.0f) {
    clearPrevious();
    return;
  }

  float frac = clampf(remainingSec / totalSec, 0.0f, 1.0f);
  float rawAngle = 270.0f + 360.0f * frac;
  float screenAngle = normalizeAngle(rawAngle - 270.0f);

  if (cache.blinkVisible && fabsf(screenAngle - cache.blinkAngleDeg) <= ANGLE_EPS) {
    return;
  }

  clearPrevious();

  float rad = deg2rad(rawAngle);
  int16_t x = CX + static_cast<int16_t>(lrintf(cosf(rad) * (R_OUT - 2)));
  int16_t y = CY + static_cast<int16_t>(lrintf(sinf(rad) * (R_OUT - 2)));
  tft.fillCircle(x, y, 5, COL_LIGHTRED);

  cache.blinkVisible = true;
  cache.blinkX = x;
  cache.blinkY = y;
  cache.blinkAngleDeg = screenAngle;
}

void showCenterText(const String &s, uint8_t textSize, uint16_t color, uint16_t bg) {
  tft.setTextColor(color, bg);
  tft.setTextSize(textSize);
  tft.setTextWrap(false);
  drawCenterText(s, bg);
  tft.print(s);
}

void showCenterText(const char *s, uint8_t textSize, uint16_t color, uint16_t bg) {
  showCenterText(String(s), textSize, color, bg);
}

void drawCenterText(const String &s, uint16_t bg) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  int16_t x = CX - static_cast<int16_t>(w) / 2;
  int16_t y = CY - static_cast<int16_t>(h) / 2;
  tft.fillCircle(CX, CY, (w > h ? w : h) / 1.2f + CENTER_CLEAR_PADDING, bg);
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

