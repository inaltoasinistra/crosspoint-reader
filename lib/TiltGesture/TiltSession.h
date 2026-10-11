#pragma once

#include <cstdint>

// Whether tilt page turning is live for the current power-on session. Held only
// in RAM: wake from deep sleep resets the chip, so a per-session arm is dropped
// on every sleep without any explicit disarm or SD write.
class TiltSession {
 public:
  // perSession: start disarmed and wait for toggle(); otherwise start enabled.
  void begin(const bool perSession) {
    this->perSession = perSession;
    enabled = !perSession;
    lastMode = UNKNOWN_MODE;
  }

  // Switching activation while running is the user configuring tilt right now, so it stays live
  // until the next sleep instead of disarming like a fresh boot.
  void changeActivation(const bool perSession) {
    this->perSession = perSession;
    enabled = true;
  }

  // Feed the configured tilt page-turn mode on every poll. Under per-session activation, picking a
  // direction in settings arms the session like the long press does; the first observation after
  // begin() is the stored value and only records it, so boot and wake stay disarmed.
  // Returns true when this call armed the session.
  bool observeMode(const uint8_t mode) {
    const bool picked = lastMode != UNKNOWN_MODE && mode != lastMode && mode != 0;
    lastMode = mode;
    if (perSession && picked && !enabled) {
      enabled = true;
      return true;
    }
    return false;
  }

  bool toggle() {
    enabled = !enabled;
    return enabled;
  }

  bool isEnabled() const { return enabled; }

  // The tilt page-turn mode to apply: the configured one while enabled, off (0) otherwise.
  uint8_t gate(const uint8_t mode) const { return enabled ? mode : 0; }

  // Whether tilt page turning is live: the configured mode is on and the session is enabled.
  bool isActive(const uint8_t mode) const { return gate(mode) != 0; }

 private:
  static constexpr uint8_t UNKNOWN_MODE = 0xFF;

  bool enabled = true;
  bool perSession = false;
  uint8_t lastMode = UNKNOWN_MODE;
};
