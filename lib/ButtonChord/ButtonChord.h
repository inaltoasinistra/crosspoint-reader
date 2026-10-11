#pragma once

#include <cstdint>

// A two-button chord such as Power + side Up. Hardware-free so the host test suite can drive it with
// synthetic button states. The two buttons must sit on separately readable inputs: the front buttons
// share one ADC ladder that decodes a single button at a time.
class ButtonChord {
 public:
  // Feed the raw state of both buttons on every poll. Returns true on the poll where the chord fires.
  bool update(const bool firstDown, const bool secondDown) {
    const bool anyDown = firstDown || secondDown;
    // Stay held through the poll that releases the last button, so its release edge is swallowed too.
    if (held && !anyDown && !wasAnyDown) held = false;
    const bool bothDown = firstDown && secondDown;
    const bool fired = bothDown && !wasBothDown;
    if (fired) held = true;
    wasBothDown = bothDown;
    wasAnyDown = anyDown;
    return fired;
  }

  // The chord fired and its buttons are not fully released yet; their input must not reach activities.
  bool isHeld() const { return held; }

  void reset() { held = wasBothDown = wasAnyDown = false; }

 private:
  bool held = false;
  bool wasBothDown = false;
  bool wasAnyDown = false;
};

// Owns both buttons from the start of a hold through their final release frame.
// An interrupted hold stays consumed until both buttons are released.
class HoldButtonChord {
 public:
  static constexpr uint32_t HOLD_MS = 2000;

  bool update(const bool firstDown, const bool secondDown, const uint32_t nowMs) {
    const bool anyDown = firstDown || secondDown;
    const bool bothDown = firstDown && secondDown;
    if (!anyDown && !wasAnyDown) state = State::IDLE;
    wasAnyDown = anyDown;

    if (state == State::IDLE && bothDown) {
      state = State::TRACKING;
      startedAtMs = nowMs;
    }
    if (state == State::TRACKING) {
      if (!bothDown) {
        state = State::CONSUMED;
      } else if (nowMs - startedAtMs >= HOLD_MS) {
        state = State::CONSUMED;
        return true;
      }
    }
    return false;
  }

  bool isHeld() const { return state != State::IDLE; }

 private:
  enum class State : uint8_t { IDLE, TRACKING, CONSUMED };
  State state = State::IDLE;
  bool wasAnyDown = false;
  uint32_t startedAtMs = 0;
};

// A page turn fired by the press of a button that may turn out to start a chord. In "turn on press"
// mode that first button turns a page before the chord exists; firing the chord shortly after
// reverses the turn.
struct ChordTurn {
  static constexpr uint32_t UNDO_WINDOW_MS = 300;

  bool valid = false;
  bool forward = false;
  uint32_t atMs = 0;

  void record(const bool isForward, const uint32_t nowMs) {
    valid = true;
    forward = isForward;
    atMs = nowMs;
  }

  // Unsigned subtraction keeps the window correct across a millis() wrap-around.
  bool shouldUndo(const uint32_t nowMs) const { return valid && nowMs - atMs <= UNDO_WINDOW_MS; }
};
