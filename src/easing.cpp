#include "pomodoro.h"

namespace {

float cubicBezierValue(float t, float p0, float p1, float p2, float p3) {
  float u = 1.0f - t;
  return (u * u * u * p0) + (3.0f * u * u * t * p1) + (3.0f * u * t * t * p2) + (t * t * t * p3);
}

float cubicBezierDerivative(float t, float p0, float p1, float p2, float p3) {
  float u = 1.0f - t;
  return 3.0f * u * u * (p1 - p0) + 6.0f * u * t * (p2 - p1) + 3.0f * t * t * (p3 - p2);
}

float cubicBezierEase(float x, float x1, float y1, float x2, float y2) {
  x = clampf(x, 0.0f, 1.0f);
  float u = x;

  for (uint8_t i = 0; i < 5; ++i) {
    float current = cubicBezierValue(u, 0.0f, x1, x2, 1.0f) - x;
    float deriv = cubicBezierDerivative(u, 0.0f, x1, x2, 1.0f);
    if (fabsf(current) < 1e-5f || fabsf(deriv) < 1e-5f) {
      break;
    }
    u = clampf(u - current / deriv, 0.0f, 1.0f);
  }

  float solvedX = cubicBezierValue(u, 0.0f, x1, x2, 1.0f);
  if (fabsf(solvedX - x) > 1e-3f) {
    float lo = 0.0f;
    float hi = 1.0f;
    for (uint8_t i = 0; i < 6; ++i) {
      float mid = (lo + hi) * 0.5f;
      float midX = cubicBezierValue(mid, 0.0f, x1, x2, 1.0f);
      if (midX < x) {
        lo = mid;
      } else {
        hi = mid;
      }
      u = mid;
    }
  }

  return clampf(cubicBezierValue(u, 0.0f, y1, y2, 1.0f), 0.0f, 1.0f);
}

}  // namespace

float easeLinear(float t) {
  return clampf(t, 0.0f, 1.0f);
}

float easeIn(float t) {
  return cubicBezierEase(t, 0.42f, 0.0f, 1.0f, 1.0f);
}

float easeOut(float t) {
  return cubicBezierEase(t, 0.0f, 0.0f, 0.58f, 1.0f);
}

float easeInOut(float t) {
  return cubicBezierEase(t, 0.42f, 0.0f, 0.58f, 1.0f);
}

