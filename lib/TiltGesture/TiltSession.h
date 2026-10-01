#pragma once

#include <cstdint>

// Whether tilt page turning is live for the current power-on session. Held only
// in RAM: wake from deep sleep resets the chip, so a per-session arm is dropped
// on every sleep without any explicit disarm or SD write.
class TiltSession {
 public:
  // perSession: start disarmed and wait for toggle(); otherwise start enabled.
  void begin(const bool perSession) { enabled = !perSession; }

  bool toggle() {
    enabled = !enabled;
    return enabled;
  }

  bool isEnabled() const { return enabled; }

  // The tilt page-turn mode to apply: the configured one while enabled, off (0) otherwise.
  uint8_t gate(const uint8_t mode) const { return enabled ? mode : 0; }

 private:
  bool enabled = true;
};
