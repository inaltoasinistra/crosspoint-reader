"""Tests for the scripts/device_ctl.py protocol logic against a fake device (no pyserial needed)."""

from __future__ import annotations

import sys
import unittest
import zlib
from collections.abc import Callable
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import device_ctl  # noqa: E402
from device_ctl import Device, DeviceError  # noqa: E402

Responder = Callable[[str], bytes]


class FakeTransport:
    """Answers each CMD line with the bytes a responder returns, like the firmware would."""

    def __init__(self, responder: Responder) -> None:
        self.responder = responder
        self.buffer = bytearray()
        self.sent: list[str] = []

    def write(self, data: bytes) -> int:
        line = data.decode().rstrip("\n")
        self.sent.append(line)
        self.buffer += self.responder(line.removeprefix("CMD:"))
        return len(data)

    def read(self, size: int) -> bytes:
        chunk = bytes(self.buffer[:size])
        del self.buffer[:size]
        return chunk

    def readline(self) -> bytes:
        end = self.buffer.find(b"\n")
        end = len(self.buffer) if end < 0 else end + 1
        return self.read(end)


class FakeClock:
    """Advances on every read so timeouts expire without real waiting."""

    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        self.now += 0.01
        return self.now


def make_device(responder: Responder) -> tuple[Device, FakeTransport]:
    transport = FakeTransport(responder)
    return Device(transport, log=None, clock=FakeClock(), sleep=lambda _: None), transport


# A 16x2 raw frame: first pixel black, the rest white.
FRAME = bytes([0x7F, 0xFF, 0xFF, 0xFF])


def screenshot_reply(payload: bytes = b"\x01\x7f\xff\x01\xff\xff", noise: bytes = b"") -> bytes:
    """Firmware reply; `noise` lands mid-payload like a log line from another task."""
    header = f"SCREENSHOT_START:{len(payload)}:16:2:rle\n".encode()
    body = payload[:2] + noise + payload[2:]
    return header + body + f"SCREENSHOT_END:{zlib.crc32(payload):08x}\nOK SCREENSHOT\n".encode()


class TestCommand(unittest.TestCase):
    def test_dev_cli(self) -> None:
        parser = device_ctl.build_parser()
        device, transport = make_device(lambda command: (
            b"DEV serial_control=0\nOK DEV\n" if command == "DEV" else b"OK DEV\n"))
        self.assertEqual(device_ctl.execute(device, parser, parser.parse_args(["dev"])), "DEV serial_control=0")
        for value in ("on", "off"):
            device_ctl.execute(device, parser, parser.parse_args(["dev", "serial-control", value]))
        self.assertEqual(transport.sent, ["CMD:DEV", "CMD:DEV serial_control on", "CMD:DEV serial_control off"])
        with self.assertRaises(DeviceError):
            device_ctl.execute(device, parser, parser.parse_args(["dev", "serial-control"]))

    def test_info_skips_log_lines(self) -> None:
        """Log lines before the reply are echoed, not parsed."""
        device, _ = make_device(lambda _: (
            b"[12] [INF] [MEM] Free: 1\n"
            b"INFO w=792 h=528 orientation=0 heap=180000 minheap=150000 maxalloc=90000 inject=1 "
            b"activity=Home board=xteink_x3\nOK INFO\n"))
        info = device.info()
        self.assertEqual((info.width, info.height, info.orientation), (792, 528, 0))
        self.assertEqual((info.activity, info.board, info.inject), ("Home", "xteink_x3", True))

    def test_err_reply_raises(self) -> None:
        device, _ = make_device(lambda _: b"ERR PRESS unsupported\n")
        with self.assertRaisesRegex(DeviceError, "unsupported"):
            device.press("confirm")

    def test_silence_times_out(self) -> None:
        device, _ = make_device(lambda _: b"")
        with self.assertRaisesRegex(DeviceError, "no reply"):
            device.screen_hash()

    def test_press_sends_button_and_duration(self) -> None:
        device, transport = make_device(lambda _: b"OK PRESS\n")
        device.press("down", 1500)
        self.assertEqual(transport.sent, ["CMD:PRESS down 1500"])

    def test_press_clamps_duration_before_waiting(self) -> None:
        device, transport = make_device(lambda _: b"OK PRESS\n")
        waits = []
        device.sleep = waits.append
        for ms in (1, 1, -1, 20000):
            device.press("down", ms)
        self.assertEqual(transport.sent, ["CMD:PRESS down 120"] * 3 + ["CMD:PRESS down 10000"])
        self.assertEqual(waits, [0.12 + device_ctl.SETTLE_AFTER_PRESS_S] * 3 +
                         [10 + device_ctl.SETTLE_AFTER_PRESS_S])


class TestScreenshot(unittest.TestCase):
    def test_decodes_frame(self) -> None:
        device, transport = make_device(lambda _: screenshot_reply())
        bitmap = device.screenshot(native_rect=device_ctl.screenshot_codec.Rect(8, 0, 16, 2))
        self.assertEqual(transport.sent, ["CMD:SCREENSHOT rect=8,0,16,2"])
        self.assertEqual(list(bitmap[0]), [0] + [1] * 15)
        self.assertEqual(list(bitmap[1]), [1] * 16)

    def test_retries_after_interleaved_log(self) -> None:
        """A log line inside the payload shifts the bytes; the CRC catches it and the capture is retried."""
        replies = [screenshot_reply(noise=b"[99] [DBG] [GFX] x\n"), screenshot_reply()]
        device, transport = make_device(lambda _: replies.pop(0))
        bitmap = device.screenshot()
        self.assertEqual(len(transport.sent), 2)
        self.assertEqual(list(bitmap[0])[:2], [0, 1])

    def test_gives_up_when_always_corrupt(self) -> None:
        device, transport = make_device(lambda _: screenshot_reply(noise=b"!\n"))
        with self.assertRaisesRegex(DeviceError, "corrupted"):
            device.screenshot()
        self.assertEqual(len(transport.sent), 2)


class TestWaiting(unittest.TestCase):
    def test_wait_stable_returns_settled_hash(self) -> None:
        """The frame counts as settled once the same hash repeats."""
        hashes = ["aa", "bb", "cc", "cc", "cc"]
        device, transport = make_device(lambda _: f"SCREENHASH {hashes.pop(0)}\nOK SCREENHASH\n".encode())
        self.assertEqual(device.wait_stable(timeout=10), "cc")
        self.assertEqual(len(transport.sent), 5)

    def test_wait_for_matches_log(self) -> None:
        device, transport = make_device(lambda _: b"")
        transport.buffer += b"[1] [INF] [ACT] Enter Home\n[2] [INF] [ACT] Enter Settings\n"
        self.assertIn("Settings", device.wait_for(r"Enter Set", timeout=1))


    def test_wait_for_sees_line_echoed_by_earlier_command(self) -> None:
        """A log line consumed while another command awaited its reply still satisfies wait_for."""
        device, _ = make_device(lambda _: b"[5] [DBG] [SCR] Screenshot saved\nOK RELEASE\n")
        device.release("all")
        self.assertIn("saved", device.wait_for("Screenshot saved", timeout=1))
        with self.assertRaises(DeviceError):
            device.wait_for("Screenshot saved", timeout=1)


class TestScript(unittest.TestCase):
    def test_parse_script(self) -> None:
        """Steps split on newlines and ';', comments dropped, quoting honoured."""
        steps = device_ctl.parse_script("press confirm; wait 100\n# comment\nwait-for 'Enter Home'\n")
        self.assertEqual(steps, [["press", "confirm"], ["wait", "100"], ["wait-for", "Enter Home"]])
        parser = device_ctl.build_parser()
        for step in steps:
            parser.parse_args(step)


if __name__ == "__main__":
    unittest.main()
