#pragma once

#include <cstdint>

// Decides when a stream of IMU samples forms a page-turn flick. Hardware-free so
// the host test suite can drive it with synthetic samples.
enum class TiltDetectionMode : uint8_t {
  RATE,          // Fire as soon as the axis rate crosses the threshold
  ACCEL,         // As RATE, but reject samples with non-rotational (linear) acceleration
  ACCEL_RETURN,  // As ACCEL, and only fire once the opposite-direction return stroke is seen
};

struct TiltSample {
  float axisDps;   // Angular rate on the page-turn axis, positive = forward
  float accelG;    // Acceleration magnitude, 1 g at rest
  uint32_t nowMs;  // Sample timestamp
};

struct TiltResult {
  bool activity;  // A gesture-level rotation was seen (resets the sleep timer)
  bool fired;     // A page turn was recognized
  bool forward;   // Direction of the page turn, valid when fired
};

class TiltGestureDetector {
 public:
  TiltResult update(TiltDetectionMode mode, const TiltSample& sample);

  // Drop any in-progress gesture and start a fresh cooldown window.
  void reset(uint32_t nowMs);

 private:
  enum class State : uint8_t {
    IDLE,
    WAIT_NEUTRAL,  // Fired; wait for the rotation to stop before re-arming
    ARMED,         // Out-stroke seen; waiting for the return stroke
  };

  State state = State::IDLE;
  bool armedForward = false;
  uint32_t armedAtMs = 0;
  uint32_t lastFireMs = 0;

  TiltResult updateIdle(TiltDetectionMode mode, const TiltSample& sample);
  TiltResult updateArmed(const TiltSample& sample);
  TiltResult fire(bool forward, uint32_t nowMs, State next);

  // A deliberate flick is close to pure rotation, so the accelerometer should
  // still read about 1 g; a large deviation means the device is being moved.
  static bool accelOk(float accelG);

  static constexpr float OUT_THRESHOLD_DPS = 270.0f;
  static constexpr float NEUTRAL_RATE_DPS = 50.0f;
  // RETURN_THRESHOLD_DPS, ACCEL_TOLERANCE_G and the return window are initial
  // estimates to be tuned against real device traces.
  static constexpr float RETURN_THRESHOLD_DPS = 200.0f;
  static constexpr float ACCEL_TOLERANCE_G = 0.35f;
  // Ignore opposite-sign readings right after arming: sensor noise around the
  // threshold would otherwise look like an instant return.
  static constexpr uint32_t MIN_RETURN_MS = 60;
  static constexpr uint32_t MAX_RETURN_MS = 700;
  static constexpr uint32_t COOLDOWN_MS = 600;
};
