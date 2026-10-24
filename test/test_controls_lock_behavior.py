"""Compile the production lock/chord paths with simulated input frames."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src/main.cpp").read_text()
MANAGER = (ROOT / "src/activities/ActivityManager.cpp").read_text()
READER = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()


def between(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


chords = "enum class ChordAction" + between(MAIN, "enum class ChordAction", "}  // namespace")
eligibility = "bool ActivityManager::acceptsControlsLock() const {" + between(
    MANAGER, "bool ActivityManager::acceptsControlsLock() const {", "bool ActivityManager::revertChordPageTurn("
)
boot_state = "controlsLocked =" + between(MAIN, "  controlsLocked =", "  RECENT_BOOKS.loadFromFile();")
input_gate = "const ChordPoll chords = updateButtonChords();" + between(
    MAIN, "  const ChordPoll chords = updateButtonChords();", "  // Under the render lock"
)
power_sleep = "  if (!chords.held && !x4ProAwaitingClickWindow" + between(
    MAIN, "  if (!chords.held && !x4ProAwaitingClickWindow", "#if FREEINK_DEVICE_PAPERMONO"
)
reader_lock = "void EpubReaderActivity::onControlsLocked() {" + between(
    READER, "void EpubReaderActivity::onControlsLocked() {", "void EpubReaderActivity::jumpToPercent("
)
queued_turn = between(READER, "if (turnGuardActive) {\n    pendingManualTurn =", "if (fromSidePress) captureChordTurnStart();")
queue_origin = "pendingTurnFromRemote =" + between(queued_turn, "    pendingTurnFromRemote =", ";") + ";"

stub = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <string>

#include "lib/ButtonChord/ButtonChord.h"

#define LOG_DBG(...) ((void)0)

struct HalGPIO {
  static constexpr uint8_t BTN_UP = 1, BTN_DOWN = 2, BTN_POWER = 4;
  uint8_t buttons = 0;
  bool isPressed(uint8_t button) const { return (buttons & button) != 0; }
  void readPhysicalSideButtons(bool& up, bool& down) const {
    up = isPressed(BTN_UP);
    down = isPressed(BTN_DOWN);
  }
  unsigned long getPowerButtonHeldTime() const { return 1500; }
} gpio;

struct BoardConfig {
  static bool isPaperMono() { return false; }
};

struct Activity {
  std::string name = "Home";
  bool exclusive = false;
  bool requiresExclusiveStorageLoop() const { return exclusive; }
};

struct ActivityManager {
  Activity* currentActivity = nullptr;
  int lockNotifications = 0, redraws = 0;
  bool acceptsControlsLock() const;
  bool isReaderActivity() const { return currentActivity && currentActivity->name == "EpubReader"; }
  bool revertChordPageTurn(bool) { return false; }
  void onControlsLocked() { ++lockNotifications; }
  void requestUpdate() { ++redraws; }
  void requestUpdateAndWait() { ++redraws; }
} activityManager;

struct MappedInputManager {
  bool blocked = false;
  bool remote = false;
  bool hasRemoteClick() const { return remote; }
  void setInputBlocked(bool value) { blocked = value; }
  bool confirm() const { return !blocked; }
} mappedInputManager;

namespace CrossPointTiltPageTurn { constexpr uint8_t TILT_OFF = 0; }
struct TiltSensor {
  int clears = 0;
  uint8_t mode = 0;
  void update(uint8_t value, uint8_t, bool) { mode = value; }
  void clearPendingEvents() { ++clears; }
} halTiltSensor;

struct Settings {
  bool lockOnWake = false;
  uint8_t tiltPageTurn = 1, orientation = 0;
  unsigned long getPowerButtonDuration() const { return 1000; }
} SETTINGS;

struct RenderLock { ~RenderLock() {} };
int renderer = 0;
int screenshots = 0, sleeps = 0;
struct ScreenshotUtil {
  static void takeScreenshot(int) { ++screenshots; }
};
unsigned long millis() { return 2000; }
void enterDeepSleep() { ++sleeps; }
bool controlsLocked = false;
unsigned long allowSleepAt = 0;

struct EpubReaderActivity {
  bool pendingTurnFromRemote = false;
  int pendingManualTurn = 0;
  bool pendingManualTurnTouch = false;
  bool pendingTurnFromSidePress = false;
  bool automaticPageTurnActive = true;
  void onControlsLocked();
  void queueTurn(bool fromTilt);
};
"""

program = stub + eligibility + chords + reader_lock + r"""
void EpubReaderActivity::queueTurn(bool fromTilt) {
  auto& mappedInput = mappedInputManager;
  pendingManualTurn = -1;
""" + queue_origin + r"""
}

void boot(bool lockOnWake, bool isSilentReboot) {
  SETTINGS.lockOnWake = lockOnWake;
""" + boot_state + r"""
}

ChordPoll inputFrame() {
""" + input_gate + r"""
  return chords;
}

void powerSleep(const ChordPoll& chords) {
  const bool x4ProAwaitingClickWindow = false;
  const bool powerReleasedSinceWake = true;
""" + power_sleep + r"""
}

int main() {
  Activity screen;
  activityManager.currentActivity = &screen;
  boot(true, false);
  assert(controlsLocked && activityManager.acceptsControlsLock());
  assert(inputFrame().held == false && mappedInputManager.blocked);
  assert(!mappedInputManager.confirm());
  assert(halTiltSensor.mode == CrossPointTiltPageTurn::TILT_OFF);
  assert(SETTINGS.tiltPageTurn == 1);

  // The same lock state survives Home -> menu -> reader; only local input is blocked.
  for (const char* name : {"Settings", "EpubReader"}) {
    screen.name = name;
    assert(activityManager.acceptsControlsLock());
    inputFrame();
    assert(mappedInputManager.blocked);
    assert(halTiltSensor.mode == CrossPointTiltPageTurn::TILT_OFF);
  }
  screen.name = "Boot";
  inputFrame();
  assert(!mappedInputManager.blocked);
  screen.name = "Sleep";
  inputFrame();
  assert(!mappedInputManager.blocked);
  screen.name = "UsbDrive";
  screen.exclusive = true;
  inputFrame();
  assert(!mappedInputManager.blocked);
  screen.exclusive = false;
  screen.name = "Home";

  // Physical Power still sleeps a locked Home; the lock chord unlocks instead.
  gpio.buttons = HalGPIO::BTN_POWER;
  powerSleep(inputFrame());
  assert(sleeps == 1);
  gpio.buttons = 0;
  inputFrame();
  inputFrame();
  gpio.buttons = HalGPIO::BTN_POWER | HalGPIO::BTN_UP;
  const ChordPoll unlock = inputFrame();
  powerSleep(unlock);
  assert(!controlsLocked && sleeps == 1 && unlock.held);
  gpio.buttons = 0;
  inputFrame();
  inputFrame();
  assert(mappedInputManager.confirm());
  assert(halTiltSensor.mode == SETTINGS.tiltPageTurn);

  // An unlocked screenshot chord works; a locked one cannot capture.
  gpio.buttons = HalGPIO::BTN_POWER | HalGPIO::BTN_DOWN;
  inputFrame();
  assert(screenshots == 1);
  gpio.buttons = 0;
  inputFrame();
  inputFrame();
  gpio.buttons = HalGPIO::BTN_POWER | HalGPIO::BTN_UP;
  inputFrame();
  assert(controlsLocked && activityManager.lockNotifications == 1);
  gpio.buttons = 0;
  inputFrame();
  inputFrame();
  gpio.buttons = HalGPIO::BTN_POWER | HalGPIO::BTN_DOWN;
  inputFrame();
  assert(screenshots == 1);

  // A normal sleep/wake boot restores the option; a silent restart does not re-lock.
  boot(true, false);
  assert(controlsLocked && inputFrame().held == false && mappedInputManager.blocked);
  boot(true, true);
  assert(!controlsLocked);
  boot(false, false);
  assert(!controlsLocked);

  EpubReaderActivity reader;
  reader.pendingManualTurn = 1;
  reader.pendingManualTurnTouch = true;
  reader.pendingTurnFromSidePress = true;
  reader.onControlsLocked();
  assert(reader.pendingManualTurn == 0 && !reader.pendingManualTurnTouch);
  assert(!reader.pendingTurnFromSidePress && reader.automaticPageTurnActive);
  reader.queueTurn(true);
  reader.onControlsLocked();
  assert(reader.pendingManualTurn == 0 && reader.automaticPageTurnActive);
  mappedInputManager.remote = true;
  reader.queueTurn(false);
  reader.onControlsLocked();
  assert(reader.pendingManualTurn == -1 && reader.automaticPageTurnActive);
  // If gyro and remote events coincide, preserve no gyro-derived queued turn.
  reader.queueTurn(true);
  reader.onControlsLocked();
  assert(reader.pendingManualTurn == 0 && reader.automaticPageTurnActive);
}
"""

with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "controls_lock.cpp"
    executable = Path(directory) / "controls_lock"
    source.write_text(program)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-I", str(ROOT), str(source), "-o", str(executable)],
                   check=True)
    subprocess.run([str(executable)], check=True)
