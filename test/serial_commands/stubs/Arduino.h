#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

inline unsigned long nowMs = 0;
inline unsigned long millis() { return nowMs; }
inline void delay(unsigned long ms) { nowMs += ms; }
struct HWCDC {
  static inline bool plugged = true;
  static bool isPlugged() { return plugged; }
};
struct FakeSerial {
  unsigned long writeLatencyMs = 0;
  std::string input;
  std::string output;
  int available() const { return input.size(); }
  int read() {
    const int c = static_cast<unsigned char>(input.front());
    input.erase(0, 1);
    return c;
  }
  size_t write(const uint8_t* data, size_t length) {
    nowMs += writeLatencyMs;
    output.append(reinterpret_cast<const char*>(data), length);
    return length;
  }
};
inline FakeSerial logSerial;
