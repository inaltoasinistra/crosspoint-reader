#include "HalTiltSensor.h"

#include <Logging.h>

#include <cmath>

HalTiltSensor halTiltSensor;  // Singleton instance

bool HalTiltSensor::readSample(float& gx, float& gy, float& gz, float& accelG) const {
  Imu::Sample sample;
  if (!_sdkImu.read(sample)) return false;
  gx = sample.gx;
  gy = sample.gy;
  gz = sample.gz;
  accelG = sqrtf(sample.ax * sample.ax + sample.ay * sample.ay + sample.az * sample.az);
  return true;
}

TiltDetectionMode HalTiltSensor::toDetectionMode(const uint8_t detection) {
  switch (detection) {
    case CrossPointTiltDetection::ACCEL:
      return TiltDetectionMode::ACCEL;
    case CrossPointTiltDetection::ACCEL_RETURN:
      return TiltDetectionMode::ACCEL_RETURN;
    default:
      return TiltDetectionMode::RATE;
  }
}

void HalTiltSensor::begin() {
  _available = _sdkImu.begin();
  if (_available) {
    _initMs = millis();
    _lastPollMs = millis();
    // begin() leaves the sensors sampling; stand them by until tilt page turn
    // actually wakes them, so a disabled IMU doesn't drain the battery.
    if (!_sdkImu.sleep()) {
      LOG_ERR("GYR", "IMU standby failed");
    }
    LOG_INF("GYR", "SDK IMU initialized");
    return;
  }
  LOG_ERR("GYR", "SDK IMU not found");
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.wake()) {
    LOG_ERR("GYR", "IMU wake failed");
    return false;
  }

  _lastPollMs = millis();
  _gestureDetector.reset(millis());
  _wakeMs = millis();
  _isAwake = true;
  return true;
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.sleep()) {
    LOG_ERR("GYR", "IMU sleep failed");
    return false;
  }

  clearPendingEvents();
  _gestureDetector.reset(millis());
  _isAwake = false;
  return true;
}

void HalTiltSensor::update(uint8_t mode, const uint8_t detection, const uint8_t activation, const uint8_t orientation,
                           const bool inReader) {
  if (!_available) {
    return;
  }

  if (activation != _activation) {
    _activation = activation;
    _session.begin(activation == CrossPointTiltActivation::UNTIL_SLEEP);
  }
  // A disarmed session behaves exactly like TILT_OFF, so the IMU sleeps and can't
  // turn pages or hold the device awake while it is carried around.
  mode = _session.gate(mode);

  // State machine: wake up or sleep based on the enabled flag
  if ((mode != CrossPointTiltPageTurn::TILT_OFF) && !_isAwake) {
    _isAwake = wake();
    return;
  } else if ((mode == CrossPointTiltPageTurn::TILT_OFF) && _isAwake) {
    _isAwake = !deepSleep();
    return;
  }

  // If disabled, skip the rest of the polling logic and avoid unnecessary I2C traffic in non-reader activities
  if ((mode == CrossPointTiltPageTurn::TILT_OFF) || !inReader) {
    return;
  }

  const unsigned long now = millis();
  // Stabilization: discard readings during gyro startup transient
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  float gx, gy, gz, accelG;
  if (!readSample(gx, gy, gz, accelG)) {
    return;
  }

  // Map the gyro axis to left/right tilt based on reader orientation.
  // On the X3 PCB: X axis = left/right in portrait, Y axis = left/right in landscape.
  float tiltAxis;
  switch (orientation) {
    case CrossPointOrientation::PORTRAIT:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gx : gx;
      break;
    case CrossPointOrientation::INVERTED:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gx : -gx;
      break;
    case CrossPointOrientation::LANDSCAPE_CW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gy : -gy;
      break;
    case CrossPointOrientation::LANDSCAPE_CCW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gy : gy;
      break;
    default:
      tiltAxis = gx;
      break;
  }

  // A half-armed gesture from the previous mode must not fire under the new one.
  if (detection != _detection) {
    _detection = detection;
    _gestureDetector.reset(now);
  }

  const TiltResult result = _gestureDetector.update(toDetectionMode(detection), {tiltAxis, accelG, now});
  if (result.activity) {
    _hadActivity = true;
  }
  if (result.fired) {
    if (result.forward) {
      _tiltForwardEvent = true;
    } else {
      _tiltBackEvent = true;
    }
    LOG_INF("GYR", "%s Trigger=(%.1f) dps accel=%.2f g", result.forward ? "Forward" : "Backward", tiltAxis, accelG);
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool val = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool val = _tiltBackEvent;
  _tiltBackEvent = false;
  return val;
}

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

bool HalTiltSensor::toggleSession() {
  const bool enabled = _session.toggle();
  clearPendingEvents();
  LOG_INF("GYR", "Tilt session %s", enabled ? "armed" : "disarmed");
  return enabled;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _hadActivity = false;
}
