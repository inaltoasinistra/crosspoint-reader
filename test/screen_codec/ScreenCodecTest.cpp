#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "lib/ScreenCodec/ScreenCodec.h"

namespace {

using ScreenCodec::Frame;
using ScreenCodec::Region;

using Bytes = std::vector<uint8_t>;

// Reference decoder; scripts/screenshot_codec.py implements the same format.
Bytes unpackBits(const Bytes& packed) {
  Bytes out;
  size_t i = 0;
  while (i < packed.size()) {
    const uint8_t header = packed[i++];
    if (header < 128) {
      out.insert(out.end(), packed.begin() + i, packed.begin() + i + header + 1);
      i += header + 1;
    } else if (header > 128) {
      out.insert(out.end(), 257 - header, packed[i++]);
    }
  }
  return out;
}

Bytes pack(const Bytes& row) {
  Bytes out(row.size() + row.size() / 128 + 1);  // packBitsRow()'s documented worst case
  out.resize(ScreenCodec::packBitsRow(row.data(), row.size(), out.data()));
  return out;
}

void appendToVector(void* context, const uint8_t* data, size_t length) {
  static_cast<Bytes*>(context)->insert(static_cast<Bytes*>(context)->end(), data, data + length);
}

Bytes encode(const Frame& frame, const Region& region) {
  Bytes out;
  auto workspace = std::make_unique<ScreenCodec::Workspace>();
  ScreenCodec::encode(frame, region, *workspace, appendToVector, &out);
  return out;
}

// A white panel with helpers to paint black pixels, in native coordinates.
struct Panel {
  uint16_t width;
  uint16_t height;
  Bytes pixels;

  Panel(uint16_t width, uint16_t height) : width(width), height(height), pixels(width / 8 * height, 0xFF) {}

  void setBlack(int x, int y) { pixels[y * (width / 8) + x / 8] &= static_cast<uint8_t>(~(0x80 >> (x % 8))); }
  Frame frame() const { return {pixels.data(), width, height}; }
};

bool isBlack(const Bytes& rows, uint16_t rowBytes, int x, int y) {
  return !((rows[y * rowBytes + x / 8] >> (7 - x % 8)) & 1);
}

class TestPackBits : public ::testing::Test {};

TEST_F(TestPackBits, test_golden_bytes) {
  // Pinned so the Python decoder test can assert against the same stream.
  // A trailing pair stays literal; only 3+ repeats become a run.
  const Bytes row = {0xFF, 0xFF, 0xFF, 0x00, 0x12, 0x34, 0x34};
  EXPECT_EQ(pack(row), (Bytes{0xFE, 0xFF, 0x03, 0x00, 0x12, 0x34, 0x34}));
}

TEST_F(TestPackBits, test_round_trip_edges) {
  // Run and literal spans hit the 128-byte header limit at 128 and 129.
  for (const size_t length : {1u, 2u, 127u, 128u, 129u, 256u}) {
    for (const uint8_t value : {uint8_t{0x00}, uint8_t{0xFF}}) {
      const Bytes uniform(length, value);
      EXPECT_EQ(unpackBits(pack(uniform)), uniform) << length;
    }
    Bytes alternating(length);
    for (size_t i = 0; i < length; i++) alternating[i] = i % 2 ? 0xAA : 0x55;
    EXPECT_EQ(unpackBits(pack(alternating)), alternating) << length;
  }
}

TEST_F(TestPackBits, test_random_within_bound) {
  std::mt19937 rng(1234);
  for (int trial = 0; trial < 200; trial++) {
    Bytes row(1 + rng() % 256);
    // Small alphabet so pairs, runs and literals interleave: the worst case
    // for the output bound that sizes the firmware's static row buffer.
    for (auto& b : row) b = static_cast<uint8_t>(rng() % 3);
    const Bytes packed = pack(row);
    EXPECT_EQ(unpackBits(packed), row);
    ASSERT_LE(packed.size(), row.size() + row.size() / 128 + 1);
  }
}

TEST_F(TestPackBits, test_white_row_compresses) {
  // An X3 row (792 px = 99 bytes) of white is a single run.
  EXPECT_EQ(pack(Bytes(99, 0xFF)).size(), 2u);
}

class TestEncode : public ::testing::Test {};

TEST_F(TestEncode, test_packbits_decodes_to_raw) {
  Panel panel(800, 480);
  std::mt19937 rng(7);
  for (int i = 0; i < 2000; i++) panel.setBlack(rng() % 800, rng() % 480);
  const Region full = {0, 0, panel.width, panel.height};
  // Rows are packed independently, so decoding the concatenation works.
  EXPECT_EQ(unpackBits(encode(panel.frame(), full)), panel.pixels);
}

TEST_F(TestEncode, test_crop_extracts_region) {
  Panel panel(800, 480);
  panel.setBlack(17, 33);
  const Region region = {16, 32, 16, 4};
  const Bytes out = unpackBits(encode(panel.frame(), region));
  ASSERT_EQ(out.size(), 2u * 4u);
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 16; x++) EXPECT_EQ(isBlack(out, 2, x, y), x == 1 && y == 1) << x << "," << y;
  }
}

class TestValidRequest : public ::testing::Test {};

TEST_F(TestValidRequest, test_rejects_bad_requests) {
  const Panel panel(800, 480);
  const Frame frame = panel.frame();
  EXPECT_TRUE(ScreenCodec::isValidRequest(frame, {0, 0, 800, 480}));
  EXPECT_TRUE(ScreenCodec::isValidRequest(frame, {8, 2, 16, 3}));
  EXPECT_FALSE(ScreenCodec::isValidRequest(frame, {4, 0, 16, 4}));    // unaligned x
  EXPECT_FALSE(ScreenCodec::isValidRequest(frame, {0, 0, 12, 4}));    // unaligned width
  EXPECT_FALSE(ScreenCodec::isValidRequest(frame, {792, 0, 16, 4}));  // past right edge
  EXPECT_FALSE(ScreenCodec::isValidRequest(frame, {0, 478, 8, 4}));   // past bottom edge
  EXPECT_FALSE(ScreenCodec::isValidRequest(frame, {0, 0, 0, 4}));     // empty
}

class TestCrc32 : public ::testing::Test {};

TEST_F(TestCrc32, test_check_value_and_chaining) {
  const Bytes check = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(ScreenCodec::crc32(0, check.data(), check.size()), 0xCBF43926u);
  const uint32_t head = ScreenCodec::crc32(0, check.data(), 4);
  EXPECT_EQ(ScreenCodec::crc32(head, check.data() + 4, check.size() - 4), 0xCBF43926u);
}

}  // namespace
