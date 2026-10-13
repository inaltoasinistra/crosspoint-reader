#pragma once

class GfxRenderer;

// Host commands on the USB serial port: one "CMD:<NAME> [args]" per line,
// answered with "OK <NAME>" or "ERR <NAME> <reason>": screen capture, INFO,
// DEV settings and button injection (PRESS/HOLD/RELEASE). Built only with
// -DSERIAL_TEST_HOOKS (env:default), so release firmware pays no flash or RAM
// and ignores serial input. scripts/device_ctl.py is the host side and
// documents the wire format.
namespace SerialCommands {

#ifdef SERIAL_TEST_HOOKS
void begin();

// Never blocks waiting for input. Commands do not count as user activity, so
// the idle low-power path is exercised between injected presses.
void poll(const GfxRenderer& renderer);

// True while a computer is attached (with a 2 s grace for USB flaps) and
// serial control is enabled. Blocks only auto-sleep.
bool hostKeepsAwake();

// True once after the host went away or serial control was disabled. The
// inactivity timer kept running while auto-sleep was held off, so the caller
// restarts it; otherwise unplugging an idle reader would sleep within seconds.
bool hostLeft();
#else
inline void begin() {}
inline void poll(const GfxRenderer&) {}
inline bool hostKeepsAwake() { return false; }
inline bool hostLeft() { return false; }
#endif

}  // namespace SerialCommands
