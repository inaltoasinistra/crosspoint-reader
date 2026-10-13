#!/usr/bin/env python3
"""Drive a USB-connected reader for automated testing.

One subcommand per call, so an agent or a shell script can chain them:

    device_ctl.py info
    device_ctl.py press confirm
    device_ctl.py wait-stable
    device_ctl.py screenshot home.png
    device_ctl.py screenshot label.png --crop 0,60,480,40
    device_ctl.py run steps.txt          # same subcommands, one per line or ';'-separated

Buttons are physical (back, confirm, left, right, up, down, power); the firmware
applies the user's remapping as for a real press. Injection needs the default
build and `device_ctl.py dev serial-control on`. Firmware log lines received
while a command runs are echoed to stderr.

Needs pyserial; PlatformIO's interpreter has it:
    ~/.platformio/penv/bin/python scripts/device_ctl.py info
"""

from __future__ import annotations

import argparse
import glob
import re
import shlex
import sys
import time
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol, TextIO

# Optional so the protocol logic can be imported and tested without pyserial.
try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

sys.path.insert(0, str(Path(__file__).resolve().parent))

import screenshot_codec  # noqa: E402  (sibling module, path set above)

BUTTONS = ("back", "confirm", "left", "right", "up", "down", "power")
COMMAND_TIMEOUT_S = 5.0
# A full raw X3 frame is ~52 KB; leave room for a slow host or a busy device.
SCREENSHOT_TIMEOUT_S = 20.0
DEFAULT_PRESS_MS = 150
MIN_PRESS_MS = 120
MAX_PRESS_MS = 10000
# Log lines that arrived while another command was waiting for its reply.
BACKLOG_LINES = 500
# The firmware times a press from its first sample, up to one 50 ms idle
# poll after the command; then the activity needs to react to the release.
SETTLE_AFTER_PRESS_S = 0.1
ESPRESSIF_USB_VID = 0x303A


class Transport(Protocol):
    def write(self, data: bytes) -> int | None: ...

    def read(self, size: int) -> bytes: ...

    def readline(self) -> bytes: ...


class DeviceError(RuntimeError):
    """The device answered ERR, or did not answer in time."""


@dataclass
class Info:
    width: int
    height: int
    orientation: int
    heap: int
    min_heap: int
    max_alloc: int
    inject: bool
    activity: str
    board: str
    raw: str


def parse_info(line: str) -> Info:
    fields = dict(re.findall(r"(\w+)=(\S+)", line))
    try:
        return Info(
            width=int(fields["w"]),
            height=int(fields["h"]),
            orientation=int(fields["orientation"]),
            heap=int(fields["heap"]),
            min_heap=int(fields["minheap"]),
            max_alloc=int(fields["maxalloc"]),
            inject=fields["inject"] == "1",
            activity=fields["activity"],
            board=fields["board"],
            raw=line,
        )
    except (KeyError, ValueError) as error:
        raise DeviceError(f"unexpected INFO line: {line!r}") from error


class Device:
    def __init__(self, transport: Transport, log: TextIO | None = sys.stderr,
                 clock: Callable[[], float] = time.monotonic, sleep: Callable[[float], None] = time.sleep) -> None:
        self.transport = transport
        self.log = log
        self.clock = clock
        self.sleep = sleep
        # A log line can arrive while an earlier command is still reading its
        # reply (e.g. "Screenshot saved" during `release`). wait_for() checks it.
        self.backlog: deque[str] = deque(maxlen=BACKLOG_LINES)

    def _readline(self, deadline: float) -> str | None:
        while self.clock() < deadline:
            raw = self.transport.readline()
            if raw:
                return raw.decode("utf-8", errors="replace").rstrip("\r\n")
        return None

    def _echo(self, line: str) -> None:
        if not line:
            return
        self.backlog.append(line)
        if self.log is not None:
            print(f"  | {line}", file=self.log)

    def _send(self, name: str, args: str = "") -> None:
        self.transport.write(f"CMD:{name} {args}".rstrip().encode() + b"\n")

    def _await_reply(self, name: str, deadline: float, keep: str | None = None) -> str | None:
        """Reads until OK/ERR for `name`; returns the last line starting with `keep`."""
        kept = None
        while True:
            line = self._readline(deadline)
            if line is None:
                raise DeviceError(f"no reply to {name}")
            if line == f"OK {name}":
                return kept
            if line.startswith(f"ERR {name}"):
                raise DeviceError(line)
            if keep is not None and line.startswith(keep):
                kept = line
            else:
                self._echo(line)

    def command(self, name: str, args: str = "", keep: str | None = None) -> str | None:
        self._send(name, args)
        return self._await_reply(name, self.clock() + COMMAND_TIMEOUT_S, keep)

    def info(self) -> Info:
        return parse_info(self.command("INFO", keep="INFO "))

    def dev(self, setting: str | None = None, value: str | None = None) -> str | None:
        if (setting is None) != (value is None):
            raise DeviceError("dev expects a setting and on|off, or no arguments")
        args = f"{setting.replace('-', '_')} {value}" if setting is not None else ""
        return self.command("DEV", args, keep="DEV ")

    def screen_hash(self) -> str:
        return self.command("SCREENHASH", keep="SCREENHASH ").split()[1]

    def press(self, button: str, ms: int = DEFAULT_PRESS_MS) -> None:
        ms = max(MIN_PRESS_MS, min(ms, MAX_PRESS_MS))
        self.command("PRESS", f"{button} {ms}")
        # The firmware releases on its own clock; don't overlap the next command.
        self.sleep(ms / 1000 + SETTLE_AFTER_PRESS_S)

    def hold(self, button: str) -> None:
        self.command("HOLD", button)

    def release(self, button: str) -> None:
        self.command("RELEASE", button)

    def wait_stable(self, timeout: float, interval: float = 0.3, repeats: int = 2) -> str:
        """Waits until the frame stops changing, i.e. a render has finished."""
        deadline = self.clock() + timeout
        last, same = None, 0
        while self.clock() < deadline:
            current = self.screen_hash()
            same = same + 1 if current == last else 0
            if same >= repeats:
                return current
            last = current
            self.sleep(interval)
        raise DeviceError(f"screen still changing after {timeout:.1f}s")

    def wait_for(self, pattern: str, timeout: float) -> str:
        """Matches lines received since the previous wait_for() first, then new ones."""
        regex = re.compile(pattern)
        earlier = list(self.backlog)
        self.backlog.clear()
        for line in earlier:
            if regex.search(line):
                return line
        deadline = self.clock() + timeout
        while (line := self._readline(deadline)) is not None:
            self._echo(line)
            if regex.search(line):
                return line
        raise DeviceError(f"no log line matching {pattern!r} within {timeout:.1f}s")

    def logs(self, seconds: float, pattern: str | None) -> list[str]:
        regex = re.compile(pattern) if pattern else None
        deadline = self.clock() + seconds
        lines = []
        while (line := self._readline(deadline)) is not None:
            if regex is None or regex.search(line):
                lines.append(line)
        return lines

    def _capture_once(self, args: str) -> tuple[screenshot_codec.Header, bytes, str]:
        self._send("SCREENSHOT", args)
        deadline = self.clock() + SCREENSHOT_TIMEOUT_S
        while True:
            line = self._readline(deadline)
            if line is None:
                raise DeviceError("no screenshot header")
            if line.startswith("ERR SCREENSHOT"):
                raise DeviceError(line)
            if line.startswith(screenshot_codec.START_PREFIX):
                break
            self._echo(line)
        header = screenshot_codec.parse_header(line)
        payload = bytearray()
        while len(payload) < header.payload_bytes and self.clock() < deadline:
            payload += self.transport.read(header.payload_bytes - len(payload))
        # Interleaved log output shifts the trailer into the payload or vice
        # versa; the checks in screenshot() turn that into a retry.
        trailer = self._readline(deadline) or ""
        self._await_reply("SCREENSHOT", deadline)
        return header, bytes(payload), trailer

    def screenshot(self, native_rect: screenshot_codec.Rect | None = None,
                   attempts: int = 2) -> screenshot_codec.Bitmap:
        args = ""
        if native_rect is not None:
            args = f"rect={native_rect.x},{native_rect.y},{native_rect.width},{native_rect.height}"
        for attempt in range(attempts):
            header, payload, trailer = self._capture_once(args)
            try:
                if not trailer.startswith(screenshot_codec.END_PREFIX):
                    raise screenshot_codec.CodecError("missing trailer")
                return screenshot_codec.decode(header, payload, screenshot_codec.parse_end(trailer))
            except screenshot_codec.CodecError as error:
                if attempt == attempts - 1:
                    raise DeviceError(f"screenshot corrupted: {error}") from error
                self._echo(f"screenshot corrupted ({error}), retrying")
        raise AssertionError("unreachable")


def detect_port() -> str:
    if list_ports is not None:
        espressif = [p.device for p in list_ports.comports() if p.vid == ESPRESSIF_USB_VID]
        if espressif:
            return espressif[0]
    candidates = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/tty.usbmodem*"))
    if not candidates:
        raise DeviceError("no reader found; is it plugged in and unlocked?")
    return candidates[0]


def open_transport(port: str) -> Transport:
    if serial is None:
        raise DeviceError("pyserial missing; run with ~/.platformio/penv/bin/python")
    # Leave DTR and RTS asserted as the OS raised them on open. Dropping DTR
    # while RTS is still high is the ESP32-C3 USB-Serial-JTAG reset sequence,
    # so "clearing" both would reboot the reader on every call.
    link = serial.Serial(port, 115200, timeout=0.1)
    link.reset_input_buffer()
    return link


def parse_crop(text: str) -> screenshot_codec.Rect:
    try:
        x, y, width, height = (int(v) for v in text.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("crop must be x,y,width,height") from error
    return screenshot_codec.Rect(x, y, width, height)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: auto-detect)")
    sub = parser.add_subparsers(dest="action", required=True)

    sub.add_parser("info", help="board, screen size, orientation, current activity, heap")
    dev = sub.add_parser("dev", help="list or change persistent development settings")
    dev.add_argument("setting", nargs="?", choices=("serial-control",))
    dev.add_argument("value", nargs="?", choices=("on", "off"))
    press = sub.add_parser("press", help="press and release a button")
    press.add_argument("button", choices=BUTTONS)
    press.add_argument("--ms", type=int, default=DEFAULT_PRESS_MS, help="hold time (long press: e.g. 1500)")
    for name in ("hold", "release"):
        action = sub.add_parser(name, help=f"{name} a button (for combos and long presses)")
        action.add_argument("button", choices=BUTTONS + (("all",) if name == "release" else ()))
    sub.add_parser("hash", help="CRC32 of the current frame")
    stable = sub.add_parser("wait-stable", help="wait until the frame stops changing")
    stable.add_argument("--timeout", type=float, default=10.0)
    shot = sub.add_parser("screenshot", help="save the screen as PNG, rotated as the user sees it")
    shot.add_argument("path")
    shot.add_argument("--crop", type=parse_crop, help="logical x,y,width,height (grown to byte boundaries)")
    shot.add_argument("--native", action="store_true", help="keep the native panel orientation")
    shot.add_argument("--zoom", type=int, default=1, help="upscale the saved image, e.g. 3 for a small crop")
    logs = sub.add_parser("logs", help="print firmware log lines for a while")
    logs.add_argument("--seconds", type=float, default=5.0)
    logs.add_argument("--grep", help="only lines matching this regex")
    wait_for = sub.add_parser("wait-for", help="wait for a log line matching a regex")
    wait_for.add_argument("pattern")
    wait_for.add_argument("--timeout", type=float, default=10.0)
    wait = sub.add_parser("wait", help="sleep (useful in run scripts)")
    wait.add_argument("ms", type=int)
    run = sub.add_parser("run", help="run subcommands from a file, one per line or ';'-separated")
    run.add_argument("script")
    return parser


def save_screenshot(device: Device, args: argparse.Namespace) -> str:
    info = device.info()
    native_rect = None
    if args.crop is not None:
        native_rect = screenshot_codec.logical_to_native_rect(args.crop, info.orientation, info.width, info.height)
    bitmap = device.screenshot(native_rect)
    if not args.native:
        bitmap = screenshot_codec.to_logical(bitmap, info.orientation)
    if args.zoom > 1:
        bitmap = screenshot_codec.zoom(bitmap, args.zoom)
    screenshot_codec.write_png(bitmap, args.path)
    return f"{args.path} {len(bitmap[0])}x{len(bitmap)}"


def parse_script(text: str) -> list[list[str]]:
    steps = []
    for line in text.splitlines():
        line = line.split("#", 1)[0]
        for part in line.split(";"):
            if part.strip():
                steps.append(shlex.split(part))
    return steps


def execute(device: Device, parser: argparse.ArgumentParser, args: argparse.Namespace) -> str | None:
    action = args.action
    if action == "info":
        return device.info().raw
    if action == "dev":
        return device.dev(args.setting, args.value)
    if action == "press":
        device.press(args.button, args.ms)
    elif action == "hold":
        device.hold(args.button)
    elif action == "release":
        device.release(args.button)
    elif action == "hash":
        return device.screen_hash()
    elif action == "wait-stable":
        return device.wait_stable(args.timeout)
    elif action == "screenshot":
        return save_screenshot(device, args)
    elif action == "logs":
        return "\n".join(device.logs(args.seconds, args.grep))
    elif action == "wait-for":
        return device.wait_for(args.pattern, args.timeout)
    elif action == "wait":
        time.sleep(args.ms / 1000)
    elif action == "run":
        for step in parse_script(Path(args.script).read_text()):
            step_args = parser.parse_args(step)
            if step_args.action == "run":
                raise DeviceError("run scripts cannot nest")
            print(f"> {' '.join(step)}")
            if (output := execute(device, parser, step_args)) is not None:
                print(output)
    return None


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        transport = open_transport(args.port or detect_port())
        output = execute(Device(transport), parser, args)
    except DeviceError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if output is not None:
        print(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
