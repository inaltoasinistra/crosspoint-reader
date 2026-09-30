#include "TiltGestureDetector.h"

#include <cmath>

TiltResult TiltGestureDetector::update(const TiltDetectionMode mode, const TiltSample& sample) {
  switch (state) {
    case State::IDLE:
      return updateIdle(mode, sample);
    case State::WAIT_NEUTRAL:
      if (std::fabs(sample.axisDps) < NEUTRAL_RATE_DPS) {
        state = State::IDLE;
      }
      return {};
    case State::ARMED:
      return updateArmed(sample);
  }
  return {};
}

void TiltGestureDetector::reset(const uint32_t nowMs) {
  state = State::IDLE;
  lastFireMs = nowMs;
}

TiltResult TiltGestureDetector::updateIdle(const TiltDetectionMode mode, const TiltSample& sample) {
  if ((sample.nowMs - lastFireMs) < COOLDOWN_MS || std::fabs(sample.axisDps) <= OUT_THRESHOLD_DPS) {
    return {};
  }

  const bool forward = sample.axisDps > 0;
  switch (mode) {
    case TiltDetectionMode::RATE:
      return fire(forward, sample.nowMs, State::WAIT_NEUTRAL);
    case TiltDetectionMode::ACCEL:
      if (!accelOk(sample.accelG)) {
        return {true, false, false};
      }
      return fire(forward, sample.nowMs, State::WAIT_NEUTRAL);
    case TiltDetectionMode::ACCEL_RETURN:
      if (accelOk(sample.accelG)) {
        state = State::ARMED;
        armedForward = forward;
        armedAtMs = sample.nowMs;
      }
      return {true, false, false};
  }
  return {};
}

TiltResult TiltGestureDetector::updateArmed(const TiltSample& sample) {
  const uint32_t elapsed = sample.nowMs - armedAtMs;
  if (elapsed > MAX_RETURN_MS) {
    // The flick never completed; this sample may itself start a new gesture.
    state = State::IDLE;
    return updateIdle(TiltDetectionMode::ACCEL_RETURN, sample);
  }
  if (elapsed < MIN_RETURN_MS) {
    return {};
  }

  const bool returning = armedForward ? sample.axisDps < -RETURN_THRESHOLD_DPS : sample.axisDps > RETURN_THRESHOLD_DPS;
  if (!returning || !accelOk(sample.accelG)) {
    return {};
  }
  // The cooldown swallows the tail of the return stroke, so no neutral wait is needed.
  return fire(armedForward, sample.nowMs, State::IDLE);
}

TiltResult TiltGestureDetector::fire(const bool forward, const uint32_t nowMs, const State next) {
  state = next;
  lastFireMs = nowMs;
  return {true, true, forward};
}

bool TiltGestureDetector::accelOk(const float accelG) { return std::fabs(accelG - 1.0f) <= ACCEL_TOLERANCE_G; }
