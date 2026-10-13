#pragma once

#include <cstdint>

// Remembers when something last happened and whether that was within a fixed
// window. Unsigned subtraction keeps it correct across millis() wraparound.
class RecentActivityWindow {
 public:
  explicit constexpr RecentActivityWindow(const uint32_t windowMs) : windowMs(windowMs) {}

  void mark(const uint32_t nowMs) {
    lastMs = nowMs;
    seen = true;
  }

  bool isActive(const uint32_t nowMs) const { return seen && nowMs - lastMs < windowMs; }

 private:
  uint32_t windowMs;
  uint32_t lastMs = 0;
  bool seen = false;
};
