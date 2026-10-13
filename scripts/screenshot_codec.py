"""Decode framebuffer captures sent by the firmware's CMD:SCREENSHOT.

Wire format (src/util/SerialCommands.cpp):

    SCREENSHOT_START:<payload bytes>:<width>:<height>:rle\\n
    <payload>
    SCREENSHOT_END:<crc32 of payload, 8 hex digits>\\n

The payload is the native (unrotated) panel region, 1 bit per pixel, MSB first,
a set bit is white, rows padded to whole bytes. With "rle" every row is
PackBits-compressed on its own. Firmware older than the harness sends only
SCREENSHOT_START:<size> and a raw full 800x480 frame.

Stdlib only, so tests and the serial monitor can use it without extra packages.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

START_PREFIX = "SCREENSHOT_START:"
END_PREFIX = "SCREENSHOT_END"

# GfxRenderer::Orientation values, as reported by CMD:INFO.
PORTRAIT = 0
LANDSCAPE_CLOCKWISE = 1
PORTRAIT_INVERTED = 2
LANDSCAPE_COUNTER_CLOCKWISE = 3  # native panel orientation

LEGACY_WIDTH = 800
LEGACY_HEIGHT = 480


class CodecError(ValueError):
    """The capture is malformed or failed its checksum."""


@dataclass(frozen=True)
class Header:
    payload_bytes: int
    width: int
    height: int
    encoding: str

    @property
    def row_bytes(self) -> int:
        return (self.width + 7) // 8


@dataclass(frozen=True)
class Rect:
    x: int
    y: int
    width: int
    height: int


Bitmap = list[bytearray]  # rows of pixels, 1 = white, 0 = black


def parse_header(line: str) -> Header:
    if not line.startswith(START_PREFIX):
        raise CodecError(f"not a screenshot header: {line!r}")
    fields = line[len(START_PREFIX):].strip().split(":")
    try:
        if len(fields) == 1:
            return Header(int(fields[0]), LEGACY_WIDTH, LEGACY_HEIGHT, "raw")
        size, width, height, encoding = fields
        if encoding != "rle":
            raise CodecError(f"unknown encoding {encoding!r}")
        return Header(int(size), int(width), int(height), encoding)
    except ValueError as error:
        raise CodecError(f"bad screenshot header: {line!r}") from error


def parse_end(line: str) -> int | None:
    """Returns the CRC from the trailer, or None for legacy firmware without one."""
    if not line.startswith(END_PREFIX):
        raise CodecError(f"not a screenshot trailer: {line!r}")
    rest = line[len(END_PREFIX):].strip()
    return int(rest[1:], 16) if rest.startswith(":") else None


def unpack_bits(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        header = data[i]
        i += 1
        if header < 128:
            out += data[i:i + header + 1]
            i += header + 1
        elif header > 128:
            if i >= len(data):
                raise CodecError("truncated PackBits run")
            out += bytes([data[i]]) * (257 - header)
            i += 1
    return bytes(out)


def decode(header: Header, payload: bytes, crc: int | None) -> Bitmap:
    if len(payload) != header.payload_bytes:
        raise CodecError(f"payload is {len(payload)} bytes, header says {header.payload_bytes}")
    if crc is not None and zlib.crc32(payload) != crc:
        raise CodecError("checksum mismatch")
    if header.encoding == "rle":
        packed = unpack_bits(payload)
    elif header.encoding == "raw":
        packed = payload
    else:
        raise CodecError(f"unknown encoding {header.encoding!r}")
    if len(packed) != header.row_bytes * header.height:
        raise CodecError(f"decoded {len(packed)} bytes for a {header.width}x{header.height} image")

    rows = []
    for y in range(header.height):
        row_data = packed[y * header.row_bytes:(y + 1) * header.row_bytes]
        rows.append(bytearray((row_data[x >> 3] >> (7 - (x & 7))) & 1 for x in range(header.width)))
    return rows


def to_logical(native: Bitmap, orientation: int) -> Bitmap:
    """Rotates a native-orientation bitmap as GfxRenderer's rotateCoordinates() does."""
    height = len(native)
    width = len(native[0]) if native else 0
    if orientation == LANDSCAPE_COUNTER_CLOCKWISE:
        return [bytearray(row) for row in native]
    if orientation == LANDSCAPE_CLOCKWISE:
        return [bytearray(reversed(row)) for row in reversed(native)]
    if orientation == PORTRAIT:
        # logical (x, y) -> native (y, height - 1 - x)
        return [bytearray(native[height - 1 - x][y] for x in range(height)) for y in range(width)]
    if orientation == PORTRAIT_INVERTED:
        # logical (x, y) -> native (width - 1 - y, x)
        return [bytearray(native[x][width - 1 - y] for x in range(height)) for y in range(width)]
    raise CodecError(f"unknown orientation {orientation}")


def logical_to_native_rect(rect: Rect, orientation: int, panel_width: int, panel_height: int) -> Rect:
    """Maps a logical (on-screen) rectangle to the smallest native region the firmware accepts.

    The firmware needs x and width on byte boundaries, so the region grows
    outward to fit and is clamped to the panel.
    """

    def to_native(x: int, y: int) -> tuple[int, int]:
        if orientation == PORTRAIT:
            return y, panel_height - 1 - x
        if orientation == LANDSCAPE_CLOCKWISE:
            return panel_width - 1 - x, panel_height - 1 - y
        if orientation == PORTRAIT_INVERTED:
            return panel_width - 1 - y, x
        if orientation == LANDSCAPE_COUNTER_CLOCKWISE:
            return x, y
        raise CodecError(f"unknown orientation {orientation}")

    if rect.width <= 0 or rect.height <= 0:
        raise CodecError("empty rectangle")
    x0, y0 = to_native(rect.x, rect.y)
    x1, y1 = to_native(rect.x + rect.width - 1, rect.y + rect.height - 1)
    left, right = max(0, min(x0, x1)), min(panel_width - 1, max(x0, x1))
    top, bottom = max(0, min(y0, y1)), min(panel_height - 1, max(y0, y1))
    if left > right or top > bottom:
        raise CodecError("rectangle is off screen")

    left -= left % 8
    right = min(panel_width, -(-(right + 1) // 8) * 8)
    return Rect(left, top, right - left, bottom + 1 - top)


def zoom(bitmap: Bitmap, factor: int) -> Bitmap:
    """Nearest-neighbour upscale, so small crops stay legible in an image viewer."""
    return [bytearray(pixel for pixel in row for _ in range(factor)) for row in bitmap for _ in range(factor)]


def write_png(bitmap: Bitmap, path: str) -> None:
    """Writes an 8-bit grayscale PNG; 1-bit pixels become 0 or 255."""
    height = len(bitmap)
    width = len(bitmap[0]) if bitmap else 0
    raw = bytearray()
    for row in bitmap:
        raw.append(0)  # filter type: none
        raw += bytes(255 if pixel else 0 for pixel in row)

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
