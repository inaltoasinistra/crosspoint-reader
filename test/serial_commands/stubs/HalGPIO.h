#pragma once
#include <cstdint>
struct HalGPIO {
  enum { BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_POWER };
  static inline uint8_t (*injected)() = nullptr;
  bool setInjectedButtonSource(uint8_t (*source)()) {
    injected = source;
    return true;
  }
};
inline HalGPIO gpio;
