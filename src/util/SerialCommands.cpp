#include "SerialCommands.h"

#ifdef SERIAL_TEST_HOOKS
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <ScreenCodec.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "util/DevSettings.h"
#include "util/RecentActivityWindow.h"

namespace SerialCommands {

namespace {

constexpr char COMMAND_PREFIX[] = "CMD:";
constexpr size_t COMMAND_PREFIX_LENGTH = sizeof(COMMAND_PREFIX) - 1;
constexpr size_t MAX_LINE_LENGTH = 64;
// The UI is stalled while a capture is sent, so give up on a host that stops reading.
constexpr unsigned long SEND_TIMEOUT_MS = 5000;

// Static so a partial line survives between poll() calls without a heap String.
char lineBuffer[MAX_LINE_LENGTH];
size_t lineLength = 0;
bool lineOverflowed = false;

// logSerial runs with a 1 ms TX timeout (see setup()), so a single write()
// drops whatever does not fit in the ring buffer. Keep writing the remainder
// until the host has drained it all.
bool writeAll(const uint8_t* data, size_t length, const unsigned long deadline) {
  while (length > 0) {
    const size_t written = logSerial.write(data, length);
    data += written;
    length -= written;
    if (length == 0) break;
    if (static_cast<long>(millis() - deadline) >= 0) return false;
    if (written == 0) delay(1);
  }
  return true;
}

void sendLine(const char* format, ...) {
  char buffer[160];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (length <= 0) return;
  const size_t size = std::min(static_cast<size_t>(length), sizeof(buffer) - 1);
  writeAll(reinterpret_cast<const uint8_t*>(buffer), size, millis() + SEND_TIMEOUT_MS);
}

// Call only under RenderLock: while a section build has the framebuffer lent
// out as scratch (FrameBufferLoan), its pointer is null.
ScreenCodec::Frame currentFrame() {
  return {display.getFrameBuffer(), display.getDisplayWidth(), display.getDisplayHeight()};
}

struct Measurement {
  size_t bytes;
  uint32_t crc;
};

void measure(void* context, const uint8_t* data, const size_t length) {
  auto* measurement = static_cast<Measurement*>(context);
  measurement->bytes += length;
  measurement->crc = ScreenCodec::crc32(measurement->crc, data, length);
}

struct Transfer {
  unsigned long deadline;
  bool complete;
};

void transmit(void* context, const uint8_t* data, const size_t length) {
  auto* transfer = static_cast<Transfer*>(context);
  if (transfer->complete) transfer->complete = writeAll(data, length, transfer->deadline);
}

bool parseScreenshotArgs(char** rest, ScreenCodec::Region& region) {
  region = {0, 0, display.getDisplayWidth(), display.getDisplayHeight()};
  while (const char* arg = strtok_r(nullptr, " ", rest)) {
    if (strncmp(arg, "rect=", 5) == 0) {
      if (sscanf(arg + 5, "%hu,%hu,%hu,%hu", &region.x, &region.y, &region.width, &region.height) != 4) return false;
    } else {
      return false;
    }
  }
  return true;
}

// The payload size goes in the header, so the region is encoded twice: once
// to measure and checksum it, once to send it. Both passes run under the
// render lock so the frame cannot change in between.
void handleScreenshot(char** rest) {
  ScreenCodec::Region region{};
  if (!parseScreenshotArgs(rest, region)) {
    sendLine("ERR SCREENSHOT bad-args\n");
    return;
  }
  auto workspace = makeUniqueNoThrow<ScreenCodec::Workspace>();
  if (!workspace) {
    LOG_ERR("SER", "OOM: screenshot workspace %u bytes", static_cast<unsigned>(sizeof(ScreenCodec::Workspace)));
    sendLine("ERR SCREENSHOT oom\n");
    return;
  }

  RenderLock lock;
  const ScreenCodec::Frame frame = currentFrame();
  if (frame.pixels == nullptr) {
    sendLine("ERR SCREENSHOT busy\n");
    return;
  }
  if (!ScreenCodec::isValidRequest(frame, region)) {
    sendLine("ERR SCREENSHOT bad-args\n");
    return;
  }
  Measurement measurement{0, 0};
  ScreenCodec::encode(frame, region, *workspace, measure, &measurement);
  // The payload size stays the first field, so a host that only reads it still
  // consumes exactly the payload and stays in sync with the stream.
  sendLine("SCREENSHOT_START:%u:%u:%u:rle\n", static_cast<unsigned>(measurement.bytes), region.width, region.height);
  Transfer transfer{millis() + SEND_TIMEOUT_MS, true};
  ScreenCodec::encode(frame, region, *workspace, transmit, &transfer);
  sendLine("SCREENSHOT_END:%08lx\n", static_cast<unsigned long>(measurement.crc));
  if (transfer.complete) {
    sendLine("OK SCREENSHOT\n");
  } else {
    sendLine("ERR SCREENSHOT timeout\n");
  }
}

void handleScreenHash() {
  uint32_t crc;
  {
    RenderLock lock;
    const ScreenCodec::Frame frame = currentFrame();
    if (frame.pixels == nullptr) {
      sendLine("ERR SCREENHASH busy\n");
      return;
    }
    crc = ScreenCodec::crc32(0, frame.pixels, static_cast<size_t>(frame.width / 8) * frame.height);
  }
  sendLine("SCREENHASH %08lx\n", static_cast<unsigned long>(crc));
  sendLine("OK SCREENHASH\n");
}

bool injectionInstalled = false;

bool serialControlEnabled() { return DevSettings::getInstance().serialControl; }

void handleInfo(const GfxRenderer& renderer) {
  const auto heap = HalMemory::getInternalHeap();
  // Free-text fields go last; neither activity nor board names contain spaces.
  sendLine("INFO w=%u h=%u orientation=%d heap=%u minheap=%u maxalloc=%u inject=%d activity=%s board=%s\n",
           display.getDisplayWidth(), display.getDisplayHeight(), static_cast<int>(renderer.getOrientation()),
           static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.minFreeBytes),
           static_cast<unsigned>(heap.largestBlockBytes), injectionInstalled && serialControlEnabled() ? 1 : 0,
           activityManager.currentActivityName(), BoardConfig::ACTIVE.name);
  sendLine("OK INFO\n");
}

// After 3 s without input the main loop idles in 50 ms slices, and
// InputManager commits an edge only when two consecutive polls agree, so a
// press must span at least two idle polls. Injected presses do not wake the
// idle loop early the way a real contact does (rawInputActive() reads pins).
constexpr unsigned long MIN_PRESS_MS = 120;
constexpr unsigned long DEFAULT_PRESS_MS = 150;
constexpr unsigned long MAX_PRESS_MS = 10000;

struct ButtonName {
  const char* name;
  uint8_t index;
};

// Physical buttons, so remapping and orientation in MappedInputManager are
// exercised exactly as for a real press.
constexpr ButtonName BUTTON_NAMES[] = {
    {"back", HalGPIO::BTN_BACK},   {"confirm", HalGPIO::BTN_CONFIRM}, {"left", HalGPIO::BTN_LEFT},
    {"right", HalGPIO::BTN_RIGHT}, {"up", HalGPIO::BTN_UP},           {"down", HalGPIO::BTN_DOWN},
    {"power", HalGPIO::BTN_POWER},
};

// A host counts as gone only after missing for this long: the USB
// start-of-frame check can flap briefly at the low-power CPU clock.
constexpr uint32_t HOST_GRACE_MS = 2000;
RecentActivityWindow hostSeen(HOST_GRACE_MS);

uint8_t injectedMask = 0;
uint8_t timedMask = 0;
unsigned long timedDurationMs = 0;
unsigned long timedReleaseAt = 0;
bool timedStarted = false;

// HalGPIO ORs this into the raw button sample before debounce, so an
// injected press takes the same path as an ADC/GPIO reading. It runs inside
// gpio.update() on the loop task, the same task as poll(), so no locking.
// A timed press is measured from its first sample, not from the command, so
// a loop stalled by SD or layout work cannot shorten it.
uint8_t injectedButtons() {
  if (timedMask != 0 && !timedStarted) {
    timedStarted = true;
    timedReleaseAt = millis() + timedDurationMs;
  } else if (timedMask != 0 && static_cast<long>(millis() - timedReleaseAt) >= 0) {
    injectedMask &= static_cast<uint8_t>(~timedMask);
    timedMask = 0;
  }
  return injectedMask;
}

bool hostWasPresent = false;
bool hostLeftPending = false;

void releaseHostControl() {
  injectedMask = 0;
  timedMask = 0;
  hostLeftPending = true;
}

void handleDev(char** rest) {
  auto& settings = DevSettings::getInstance();
  const struct {
    const char* name;
    bool* value;
  } fields[] = {{"serial_control", &settings.serialControl}};
  const char* name = strtok_r(nullptr, " ", rest);
  if (name == nullptr) {
    for (const auto& field : fields) sendLine("DEV %s=%d\n", field.name, *field.value ? 1 : 0);
    sendLine("OK DEV\n");
    return;
  }
  const char* value = strtok_r(nullptr, " ", rest);
  if (value == nullptr || (strcmp(value, "on") != 0 && strcmp(value, "off") != 0) ||
      strtok_r(nullptr, " ", rest) != nullptr) {
    sendLine("ERR DEV bad-args\n");
    return;
  }
  for (const auto& field : fields) {
    if (strcmp(name, field.name) != 0) continue;
    const bool enabled = strcmp(value, "on") == 0;
    const bool previous = *field.value;
    if (previous != enabled) {
      *field.value = enabled;
      if (!settings.saveToFile()) {
        *field.value = previous;
        sendLine("ERR DEV save-failed\n");
        return;
      }
      if (!settings.serialControl) releaseHostControl();
    }
    sendLine("OK DEV\n");
    return;
  }
  sendLine("ERR DEV bad-args\n");
}

// On the host's departure, drop injected input (a crashed host can leave a
// HOLD behind that nobody can RELEASE) and report it once via hostLeft().
void trackHost() {
#if ARDUINO_USB_CDC_ON_BOOT
  // isPlugged() is a timer-based check for the host's USB start-of-frame
  // packets; a wall charger sends none.
  const uint32_t now = millis();
  if (HWCDC::isPlugged()) hostSeen.mark(now);
  const bool present = hostSeen.isActive(now);
  if (hostWasPresent && !present) {
    releaseHostControl();
  }
  hostWasPresent = present;
#endif
}

int buttonBit(const char* name) {
  if (name == nullptr) return -1;
  for (const auto& button : BUTTON_NAMES) {
    if (strcmp(name, button.name) == 0) return 1 << button.index;
  }
  return -1;
}

void handlePress(char** rest) {
  const int bit = buttonBit(strtok_r(nullptr, " ", rest));
  if (bit < 0) {
    sendLine("ERR PRESS bad-button\n");
    return;
  }
  if (timedMask != 0 || (injectedMask & bit)) {
    sendLine("ERR PRESS busy\n");
    return;
  }
  const char* duration = strtok_r(nullptr, " ", rest);
  unsigned long ms = duration ? strtoul(duration, nullptr, 10) : DEFAULT_PRESS_MS;
  ms = std::max(MIN_PRESS_MS, std::min(ms, MAX_PRESS_MS));
  injectedMask |= static_cast<uint8_t>(bit);
  timedMask = static_cast<uint8_t>(bit);
  timedDurationMs = ms;
  timedStarted = false;
  sendLine("OK PRESS\n");
}

void handleHold(char** rest) {
  const int bit = buttonBit(strtok_r(nullptr, " ", rest));
  if (bit < 0) {
    sendLine("ERR HOLD bad-button\n");
    return;
  }
  injectedMask |= static_cast<uint8_t>(bit);
  sendLine("OK HOLD\n");
}

void handleRelease(char** rest) {
  const char* name = strtok_r(nullptr, " ", rest);
  const int bit = name && strcmp(name, "all") == 0 ? 0xFF : buttonBit(name);
  if (bit < 0) {
    sendLine("ERR RELEASE bad-button\n");
    return;
  }
  injectedMask &= static_cast<uint8_t>(~bit);
  timedMask &= static_cast<uint8_t>(~bit);
  sendLine("OK RELEASE\n");
}

// Returns false when the command is not an injection command.
bool dispatchInjection(const char* name, char** rest) {
  void (*handler)(char**) = nullptr;
  if (strcmp(name, "PRESS") == 0) {
    handler = handlePress;
  } else if (strcmp(name, "HOLD") == 0) {
    handler = handleHold;
  } else if (strcmp(name, "RELEASE") == 0) {
    handler = handleRelease;
  } else {
    return false;
  }
  if (!serialControlEnabled()) {
    sendLine("ERR %s disabled\n", name);
  } else if (injectionInstalled) {
    handler(rest);
  } else {
    sendLine("ERR %s unsupported\n", name);
  }
  return true;
}

void dispatch(char* command, const GfxRenderer& renderer) {
  char* rest = nullptr;
  const char* name = strtok_r(command, " ", &rest);
  if (name == nullptr) return;
  if (strcmp(name, "SCREENSHOT") == 0) {
    handleScreenshot(&rest);
  } else if (strcmp(name, "SCREENHASH") == 0) {
    handleScreenHash();
  } else if (strcmp(name, "INFO") == 0) {
    handleInfo(renderer);
  } else if (strcmp(name, "DEV") == 0) {
    handleDev(&rest);
  } else if (!dispatchInjection(name, &rest)) {
    sendLine("ERR %s unknown\n", name);
  }
}

}  // namespace

void begin() { injectionInstalled = gpio.setInjectedButtonSource(injectedButtons); }

bool hostKeepsAwake() { return serialControlEnabled() && hostSeen.isActive(millis()); }

bool hostLeft() {
  const bool left = hostLeftPending;
  hostLeftPending = false;
  return left;
}

void poll(const GfxRenderer& renderer) {
  trackHost();
  while (logSerial.available() > 0) {
    const int c = logSerial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c != '\n') {
      if (lineLength < MAX_LINE_LENGTH - 1) {
        lineBuffer[lineLength++] = static_cast<char>(c);
      } else {
        lineOverflowed = true;
      }
      continue;
    }

    lineBuffer[lineLength] = '\0';
    const bool isCommand = !lineOverflowed && strncmp(lineBuffer, COMMAND_PREFIX, COMMAND_PREFIX_LENGTH) == 0;
    if (lineOverflowed) LOG_ERR("SER", "Dropped serial line longer than %u bytes", MAX_LINE_LENGTH - 1);
    lineLength = 0;
    lineOverflowed = false;
    if (isCommand) {
      // One command per loop iteration keeps input and rendering responsive.
      dispatch(lineBuffer + COMMAND_PREFIX_LENGTH, renderer);
      // A transfer can outlast the host-presence grace period.
      trackHost();
      return;
    }
  }
}

}  // namespace SerialCommands
#endif  // SERIAL_TEST_HOOKS
