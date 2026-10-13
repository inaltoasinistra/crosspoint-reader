# Testing and Debugging

CrossPoint runs on real hardware, so debugging usually combines local build checks and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Flash and monitor

Flash firmware:

```sh
pio run --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

For scripted on-device tests, flash `default` (`pio run -e default --target upload`) and drive it with
`scripts/device_ctl.py`. Run `device_ctl.py dev` to list development settings and
`device_ctl.py dev serial-control on` to enable button injection and prevent auto-sleep while a computer is
attached. Serial control is off by default. Use `dev serial-control off` to release injected buttons and
restart the normal sleep timeout. This setting is stored in `/.crosspoint/dev.json`, survives reboots and
release flashes, and has no Settings GUI entry. Release builds compile all serial commands out (screenshots and
`INFO` included), so they ignore serial input and never touch this file.

The serial commands are `CMD:DEV` and `CMD:DEV serial_control on|off`. Screenshots use
`CMD:SCREENSHOT [rect=x,y,w,h]` and always transfer RLE data; the monitor and `device_ctl.py` save PNGs.
Use `device_ctl.py screenshot screen.png --native` to keep native panel orientation.

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#7-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)
