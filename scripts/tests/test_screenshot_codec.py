"""Tests for scripts/screenshot_codec.py (stdlib only; run directly or via ctest)."""

from __future__ import annotations

import random
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import screenshot_codec as codec  # noqa: E402
from screenshot_codec import Rect  # noqa: E402


def pack_rows(rows: list[list[int]]) -> bytes:
    """Packs 0/1 pixel rows MSB-first, padding with white, like the firmware."""
    out = bytearray()
    for row in rows:
        padded = row + [1] * (-len(row) % 8)
        for i in range(0, len(padded), 8):
            out.append(int("".join(str(bit) for bit in padded[i:i + 8]), 2))
    return bytes(out)


def native_of(orientation: int, x: int, y: int, width: int, height: int) -> tuple[int, int]:
    """Reference copy of GfxRenderer.cpp rotateCoordinates()."""
    return {
        codec.PORTRAIT: (y, height - 1 - x),
        codec.LANDSCAPE_CLOCKWISE: (width - 1 - x, height - 1 - y),
        codec.PORTRAIT_INVERTED: (width - 1 - y, x),
        codec.LANDSCAPE_COUNTER_CLOCKWISE: (x, y),
    }[orientation]


class TestPackBits(unittest.TestCase):
    def test_golden_bytes(self) -> None:
        """Same stream as ScreenCodecTest.test_golden_bytes on the firmware side."""
        packed = bytes([0xFE, 0xFF, 0x03, 0x00, 0x12, 0x34, 0x34])
        self.assertEqual(codec.unpack_bits(packed), bytes([0xFF, 0xFF, 0xFF, 0x00, 0x12, 0x34, 0x34]))

    def test_noop_header_and_truncation(self) -> None:
        """Header 128 is a no-op; a run header without its byte is an error."""
        self.assertEqual(codec.unpack_bits(bytes([0x80, 0x00, 0xAB])), b"\xab")
        with self.assertRaises(codec.CodecError):
            codec.unpack_bits(bytes([0xFE]))


class TestHeader(unittest.TestCase):
    def test_parse_current_and_legacy(self) -> None:
        """New headers carry geometry; old firmware sends only the size of an 800x480 frame."""
        self.assertEqual(codec.parse_header("SCREENSHOT_START:1234:792:528:rle"),
                         codec.Header(1234, 792, 528, "rle"))
        self.assertEqual(codec.parse_header("SCREENSHOT_START:48000"), codec.Header(48000, 800, 480, "raw"))
        with self.assertRaises(codec.CodecError):
            codec.parse_header("SCREENSHOT_START:12:x")
        with self.assertRaises(codec.CodecError):
            codec.parse_header("SCREENSHOT_START:48000:800:480:raw")

    def test_parse_end(self) -> None:
        self.assertEqual(codec.parse_end("SCREENSHOT_END:cbf43926"), 0xCBF43926)
        self.assertIsNone(codec.parse_end("SCREENSHOT_END"))


class TestDecode(unittest.TestCase):
    rows: list[list[int]]
    raw: bytes

    def setUp(self) -> None:
        rng = random.Random(3)
        self.rows = [[rng.choice((0, 1, 1, 1)) for _ in range(20)] for _ in range(6)]
        self.raw = pack_rows(self.rows)

    def test_legacy_frame(self) -> None:
        payload = b"\x7f" + b"\xff" * 47999
        bitmap = codec.decode(codec.parse_header("SCREENSHOT_START:48000"), payload, None)
        self.assertEqual(len(bitmap), 480)
        self.assertEqual(list(bitmap[0]), [0] + [1] * 799)
        self.assertTrue(all(all(row) for row in bitmap[1:]))

    def test_rle_round_trip(self) -> None:
        # One literal span per row is valid PackBits.
        packed = b"".join(bytes([2]) + self.raw[i:i + 3] for i in range(0, len(self.raw), 3))
        header = codec.Header(len(packed), 20, 6, "rle")
        self.assertEqual([list(row) for row in codec.decode(header, packed, None)], self.rows)

    def test_rejects_corruption(self) -> None:
        """A flipped byte fails the CRC; a short payload fails the size check."""
        header = codec.Header(len(self.raw), 20, 6, "raw")
        corrupted = bytes([self.raw[0] ^ 0x01]) + self.raw[1:]
        with self.assertRaisesRegex(codec.CodecError, "checksum"):
            codec.decode(header, corrupted, zlib.crc32(self.raw))
        with self.assertRaisesRegex(codec.CodecError, "payload"):
            codec.decode(header, self.raw[:-1], None)


class TestOrientation(unittest.TestCase):
    def test_to_logical_matches_renderer(self) -> None:
        """Every logical pixel comes from the native pixel the renderer would have drawn."""
        width, height = 16, 8
        rng = random.Random(5)
        native = [bytearray(rng.choice((0, 1)) for _ in range(width)) for _ in range(height)]
        for orientation in range(4):
            logical = codec.to_logical(native, orientation)
            portrait = orientation in (codec.PORTRAIT, codec.PORTRAIT_INVERTED)
            self.assertEqual((len(logical[0]), len(logical)), (height, width) if portrait else (width, height))
            for y, row in enumerate(logical):
                for x, pixel in enumerate(row):
                    nx, ny = native_of(orientation, x, y, width, height)
                    self.assertEqual(pixel, native[ny][nx], (orientation, x, y))

    def test_crop_rect_is_valid_and_covers_request(self) -> None:
        """Mapped regions satisfy ScreenCodec::isValidRequest() and contain the requested pixels."""
        rng = random.Random(9)
        for panel_width, panel_height in ((800, 480), (792, 528)):
            for orientation in range(4):
                portrait = orientation in (codec.PORTRAIT, codec.PORTRAIT_INVERTED)
                logical_w, logical_h = (panel_height, panel_width) if portrait else (panel_width, panel_height)
                for _ in range(50):
                    x, y = rng.randrange(logical_w), rng.randrange(logical_h)
                    rect = Rect(x, y, rng.randint(1, logical_w - x), rng.randint(1, logical_h - y))
                    native = codec.logical_to_native_rect(rect, orientation, panel_width, panel_height)
                    self.assertEqual(native.x % 8, 0)
                    self.assertEqual(native.width % 8, 0)
                    self.assertLessEqual(native.x + native.width, panel_width)
                    self.assertLessEqual(native.y + native.height, panel_height)
                    for corner in ((rect.x, rect.y), (rect.x + rect.width - 1, rect.y + rect.height - 1)):
                        nx, ny = native_of(orientation, *corner, panel_width, panel_height)
                        self.assertTrue(native.x <= nx < native.x + native.width, (rect, native, orientation))
                        self.assertTrue(native.y <= ny < native.y + native.height, (rect, native, orientation))


class TestZoom(unittest.TestCase):
    def test_zoom_repeats_pixels(self) -> None:
        zoomed = codec.zoom([bytearray([0, 1])], 2)
        self.assertEqual([list(row) for row in zoomed], [[0, 0, 1, 1], [0, 0, 1, 1]])


class TestPng(unittest.TestCase):
    def test_png_pixels(self) -> None:
        bitmap = [bytearray([1, 0, 1]), bytearray([0, 0, 1])]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "shot.png"
            codec.write_png(bitmap, str(path))
            data = path.read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        width, height = struct.unpack(">II", data[16:24])
        self.assertEqual((width, height), (3, 2))
        idat_length = struct.unpack(">I", data[33:37])[0]
        pixels = zlib.decompress(data[41:41 + idat_length])
        self.assertEqual(pixels, bytes([0, 255, 0, 255, 0, 0, 0, 255]))


if __name__ == "__main__":
    unittest.main()
