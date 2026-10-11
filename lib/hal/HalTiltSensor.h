#pragma once

#include <Arduino.h>
#include <Imu.h>
#include <TiltGestureDetector.h>
#include <TiltSession.h>

// TODO: Move enums into new header and share with CrossPointSettings.h
namespace CrossPointOrientation {
enum Value : uint8_t { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3 };
}

namespace CrossPointTiltPageTurn {
enum Value : uint8_t { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2 };
}

namespace CrossPointTiltDetection {
enum Value : uint8_t { RATE = 0, ACCEL = 1, ACCEL_RETURN = 2 };
}

namespace CrossPointTiltActivation {
enum Value : uint8_t { ALWAYS = 0, UNTIL_SLEEP = 1 };
}

class HalTiltSensor;
extern HalTiltSensor halTiltSensor;  // Singleton

class HalTiltSensor {
  bool _available = false;
  mutable Imu _sdkImu;

  TiltGestureDetector _gestureDetector;
  uint8_t _detection = CrossPointTiltDetection::RATE;  // Detector is reset when this changes

  TiltSession _session;
  // The first sync (at boot) starts the session; later changes come from the user's settings.
  static constexpr uint8_t ACTIVATION_UNSET = 0xFF;
  uint8_t _activation = ACTIVATION_UNSET;

  bool _tiltForwardEvent = false;  // Consumed by wasTiltedForward()
  bool _tiltBackEvent = false;     // Consumed by wasTiltedBack()
  bool _hadActivity = false;       // Non-consuming flag for sleep timer
  bool _isAwake = false;           // Tracks power state
  unsigned long _initMs = 0;       // Timestamp of sensor init
  unsigned long _wakeMs = 0;       // Timestamp of last wake() for stabilization

  // Tuning constants
  static constexpr unsigned long POLL_INTERVAL_MS = 50;    // 20 Hz polling
  static constexpr unsigned long WAKE_STABILIZE_MS = 300;  // Ignore readings after wake

  mutable unsigned long _lastPollMs = 0;

  // Reads the gyro axes and the acceleration magnitude (g) from one IMU sample.
  bool readSample(float& gx, float& gy, float& gz, float& accelG) const;
  static TiltDetectionMode toDetectionMode(uint8_t detection);

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // Enables tilt polling state
  bool wake();

  // Puts tilt polling state to sleep
  bool deepSleep();

  // True if an IMU is present on this device
  bool isAvailable() const { return _available; }

  // Poll the IMU and update tilt gesture state.
  // mode: CrossPointTiltPageTurn, detection: CrossPointTiltDetection, activation: CrossPointTiltActivation.
  void update(const uint8_t mode, const uint8_t detection, const uint8_t activation, const uint8_t orientation,
              const bool inReader);

  // Restart the session when the activation setting changes (CrossPointTiltActivation).
  // Called once settings are loaded so the first render already sees the right session state.
  void syncActivation(uint8_t activation);

  // Arm or disarm tilt for the rest of this power-on session. Returns the new state.
  bool toggleSession();
  // mode: CrossPointTiltPageTurn. Derived from settings and session rather than the IMU power
  // state, so it is already correct on the render that follows a toggle.
  bool isTiltActive(const uint8_t mode) const { return _available && _session.isActive(mode); }

  // Returns true once per tilt-forward gesture (next page direction).
  // Consumed on read — subsequent calls return false until next gesture.
  bool wasTiltedForward();

  // Returns true once per tilt-back gesture (previous page direction).
  // Consumed on read.
  bool wasTiltedBack();

  // Non-consuming: true if any tilt activity occurred since last call.
  // Used to reset the auto-sleep inactivity timer.
  bool hadActivity();

  // Discard any pending tilt events (call when leaving reader or disabling tilt).
  void clearPendingEvents();
};
