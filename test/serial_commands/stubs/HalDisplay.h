#pragma once
#include <cstdint>
struct FakeDisplay {
  uint8_t pixels[4] = {0x7f, 0xff, 0xff, 0xff};
  const uint8_t* getFrameBuffer() const { return pixels; }
  uint16_t getDisplayWidth() const { return 16; }
  uint16_t getDisplayHeight() const { return 2; }
};
inline FakeDisplay display;
