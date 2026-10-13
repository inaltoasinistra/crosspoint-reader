#pragma once

#include <cstddef>
#include <cstdint>

// Encodes a region of the 1-bpp framebuffer for transfer to a host test tool.
// Pixels are MSB-first within a byte and a set bit is white, matching
// GfxRenderer::drawPixel(). Output is produced one row at a time so the whole
// screen never needs a second buffer.
namespace ScreenCodec {

struct Frame {
  const uint8_t* pixels;
  uint16_t width;  // native panel width; a multiple of 8
  uint16_t height;
};

// Native panel coordinates. The host maps logical (rotated) rectangles onto
// these, so the firmware does not need a second copy of the rotation rules.
struct Region {
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
};

// Receives each encoded row; data is only valid during the call.
using Sink = void (*)(void* context, const uint8_t* data, size_t length);

// Bounds the row buffers: 2048 px covers every supported panel.
static constexpr uint16_t MAX_ROW_BYTES = 256;
// PackBits worst case for one row; see packBitsRow().
static constexpr uint16_t MAX_PACKED_ROW_BYTES = MAX_ROW_BYTES + MAX_ROW_BYTES / 128 + 1;

// Scratch row for encode(), ~260 B. The caller owns it so it only exists
// while a capture is being sent.
struct Workspace {
  uint8_t packedRow[MAX_PACKED_ROW_BYTES];
};

// x and width must be byte aligned and the region must lie inside the frame.
bool isValidRequest(const Frame& frame, const Region& region);

void encode(const Frame& frame, const Region& region, Workspace& workspace, Sink sink, void* context);

// One PackBits (TIFF/Apple) row: header n in 0..127 copies n+1 literal bytes,
// header n in 129..255 repeats the next byte 257-n times. Writes at most
// length + length / 128 + 1 bytes and returns the count.
size_t packBitsRow(const uint8_t* row, size_t length, uint8_t* out);

// zlib-compatible CRC-32; start with crc = 0 and chain calls.
uint32_t crc32(uint32_t crc, const uint8_t* data, size_t length);

}  // namespace ScreenCodec
