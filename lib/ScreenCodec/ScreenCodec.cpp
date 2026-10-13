#include "ScreenCodec.h"

#include <cstring>

namespace ScreenCodec {

namespace {

constexpr size_t PACKBITS_MAX_SPAN = 128;
// Only runs of 3+ end a literal and each saves at least one byte, which pays
// for the extra literal header it causes, keeping MAX_PACKED_ROW_BYTES a bound.
constexpr size_t PACKBITS_MIN_RUN = 3;

bool startsRun(const uint8_t* row, const size_t at, const size_t length) {
  return at + PACKBITS_MIN_RUN <= length && row[at] == row[at + 1] && row[at] == row[at + 2];
}

}  // namespace

bool isValidRequest(const Frame& frame, const Region& region) {
  if (frame.pixels == nullptr || frame.width % 8 != 0) return false;
  if (region.width == 0 || region.height == 0) return false;
  if (region.x % 8 != 0 || region.width % 8 != 0) return false;
  if (region.x + region.width > frame.width || region.y + region.height > frame.height) return false;
  return region.width / 8 <= MAX_ROW_BYTES;
}

void encode(const Frame& frame, const Region& region, Workspace& workspace, const Sink sink, void* context) {
  const uint16_t stride = frame.width / 8;
  const uint16_t rowBytes = region.width / 8;
  for (uint16_t y = 0; y < region.height; y++) {
    const uint8_t* row = frame.pixels + static_cast<size_t>(region.y + y) * stride + region.x / 8;
    sink(context, workspace.packedRow, packBitsRow(row, rowBytes, workspace.packedRow));
  }
}

size_t packBitsRow(const uint8_t* row, const size_t length, uint8_t* out) {
  size_t in = 0;
  size_t written = 0;
  while (in < length) {
    size_t run = 1;
    while (in + run < length && run < PACKBITS_MAX_SPAN && row[in + run] == row[in]) run++;
    if (run >= PACKBITS_MIN_RUN) {
      out[written++] = static_cast<uint8_t>(257 - run);
      out[written++] = row[in];
      in += run;
      continue;
    }

    // A pair stays inside the literal: as its own run it would cost 2 bytes
    // plus a header to restart the literal, more than the 2 bytes it encodes.
    const size_t start = in;
    while (in < length && in - start < PACKBITS_MAX_SPAN && !startsRun(row, in, length)) in++;
    out[written++] = static_cast<uint8_t>(in - start - 1);
    memcpy(out + written, row + start, in - start);
    written += in - start;
  }
  return written;
}

uint32_t crc32(uint32_t crc, const uint8_t* data, const size_t length) {
  // Bitwise rather than table-driven: a full screen is ~52 KB, a few ms at
  // 160 MHz, which does not justify a 1 KB table.
  crc = ~crc;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

}  // namespace ScreenCodec
