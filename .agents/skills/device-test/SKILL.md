---
name: device-test
description: Drive a USB-connected reader over serial to check a UI or input change on real hardware. Use when a human has plugged in an unlocked device and asked for an on-device check, screenshots, or a UI audit.
---

# Device test

`scripts/device_ctl.py` sends button presses and captures the screen over the
USB serial port. It is the host side of `src/util/SerialCommands.cpp`. Use it to
see the behavior you changed yourself instead of describing it from the code.
Device access stays within the task the human authorized. Hardware sign-off
still belongs to the human (see `firmware-handoff`).

## 1. Connect

- Ask the human to plug in the reader and unlock it. Find it with
  `ls /dev/ttyACM*` (Linux) or `/dev/tty.usbmodem*` (macOS).
- Flash `default` (`pio run -e default -t upload`). It has `-DSERIAL_TEST_HOOKS`,
  but serial control is off by default. Release builds contain no serial commands at all.
- Run the tool with PlatformIO's Python, which has pyserial:
  `~/.platformio/penv/bin/python scripts/device_ctl.py info`. Check that
  the board and current `activity`. If `inject=0`, run
  `~/.platformio/penv/bin/python scripts/device_ctl.py dev serial-control on`, then check `info` again.
  This persists in `/.crosspoint/dev.json` across reboots and release flashes; release builds leave it alone.
  Turn it off at the end of the session only if the human asks (`dev serial-control off`).
- Only one program can own the port. Close `debugging_monitor.py` and
  `pio device monitor` first.
- While serial control is on and a computer is attached, the default build never auto-sleeps, so the
  reader is still awake after a long coding session. It still drops to its
  idle low-power mode between presses, like for a real user. Once host disconnection is detected,
  the normal inactivity timeout restarts; a wall charger sends no USB frames, so it should
  not count as a computer. The power button still sleeps the device.

## 2. Drive and look

- Buttons are physical (`back confirm left right up down power`). The firmware
  applies the user's remapping, so the result matches what a human pressing the
  same key would see. Use `press <b> --ms 1500` for a long press, and
  `hold`/`release` for combos.
- After each step run `wait-stable`; it returns once the frame stops changing.
  Then capture. Don't sleep for a fixed time instead.
- Check `info` (the `activity` field) after every step before pressing on.
  Presses can be swallowed while a screen loads (e.g. a book opening), and a
  render can start after `wait-stable` already returned. Some entries also
  have side effects, such as connecting to Wi-Fi first. A blind sequence of
  presses drifts onto the wrong screen.
- Read the code before pressing Confirm in settings: it flips toggles and
  two-value settings immediately. Back cancels option popups and the keyboard.
- Use `hash` to tell whether anything changed without transferring an image.
  Otherwise capture the full screen (RLE-compressed, ~70 ms). For text and
  pixel details, `--crop x,y,w,h` takes on-screen coordinates; add `--zoom 3`
  to small crops so they are readable.
- Read the PNGs with the image viewer tool. Screenshots come out rotated as the
  user sees them; `--native` gives the native panel orientation.
- `wait-for <regex>` and `logs --grep` read firmware logs, for example
  activity transitions or errors.
- Save repeatable sequences as a `run` script (one subcommand per line or
  `;`-separated) next to your notes.
- Before finishing, run `release all`, then return the device to a neutral
  screen and its original settings (language, orientation). Report anything
  you could not restore.

## 3. Know the limits

The harness cannot exercise these, so hand them to the human as a checklist:

- tilt and IMU behaviour
- deep-sleep wake (the CPU is off; the power pin is read directly)
- real touch
- battery, charging and power draw
- SD card insertion and removal
- e-ink ghosting and grayscale

Captures read only the black-and-white framebuffer. Anti-aliasing gray planes
are not included.

**Done when:** the changed behavior has been shown on the device with
screenshot evidence, and the remaining physical checks are listed for the human.
