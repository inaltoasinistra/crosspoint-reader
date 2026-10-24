"""Host-side contracts for reader controls that require a device to exercise end-to-end."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def method_body(path: str, start: str, end: str) -> str:
    source = (ROOT / path).read_text()
    return source.split(start, 1)[1].split(end, 1)[0]


class TestControlsLockIntegration(unittest.TestCase):
    def test_automatic_turn_continues_on_lock(self) -> None:
        body = method_body(
            "src/activities/reader/EpubReaderActivity.cpp",
            "void EpubReaderActivity::onControlsLocked() {",
            "void EpubReaderActivity::jumpToPercent(",
        )
        self.assertNotIn("automaticPageTurnActive = false;", body)

    def test_tilt_is_blocked_and_remote_turns_stay_active(self) -> None:
        main = method_body("src/main.cpp", "const ChordPoll chords = updateButtonChords();", "// Under the render lock")
        self.assertIn("inputLocked ? CrossPointTiltPageTurn::TILT_OFF : SETTINGS.tiltPageTurn", main)
        remote = method_body("src/activities/ActivityManager.cpp", "bool ActivityManager::remoteTurn(",
                             "bool ActivityManager::handleForcedRefresh(")
        self.assertNotIn("controlsLocked", remote)

    def test_delayed_power_sleep_ignores_locked_reader(self) -> None:
        body = method_body("src/main.cpp", "// Let wake continue as soon as its hold", "const unsigned long sleepTimeoutMs =")
        self.assertIn("if (inputLocked) lastX4ProPowerClickAt = 0;", body)
        self.assertIn("if (!inputLocked && !chords.held && handleX4ProFrontlightDoubleClick())", body)
        self.assertIn("if (!inputLocked && SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP", body)
        self.assertIn("if (inputLocked && !chords.held && BoardConfig::isX4Pro() && SETTINGS.doubleClickPwrLight", body)
        self.assertIn("gpio.wasReleased(HalGPIO::BTN_POWER)) {\n    enterDeepSleep();", body)

    def test_chord_screenshot_does_not_queue_second_render(self) -> None:
        body = method_body("src/main.cpp", "void runChordAction(", "ChordPoll updateButtonChords(")
        self.assertIn("revertChordPageTurn(action != ChordAction::Screenshot)", body)
        self.assertIn("if (reverted) activityManager.requestUpdateAndWait();", body)

    def test_screenshot_chord_is_blocked_while_locked(self) -> None:
        body = method_body("src/main.cpp", "ChordPoll updateButtonChords() {", "}  // namespace")
        self.assertIn("spec.action == ChordAction::Screenshot && controlsLocked && lockable", body)
        body = method_body("src/main.cpp", "void runChordAction(", "ChordPoll updateButtonChords()")
        self.assertIn("action == ChordAction::Screenshot && controlsLocked && activityManager.acceptsControlsLock()", body)

    def test_home_and_menus_accept_lock(self) -> None:
        manager = method_body("src/activities/ActivityManager.cpp", "bool ActivityManager::acceptsControlsLock() const {",
                              "bool ActivityManager::revertChordPageTurn(")
        self.assertIn('currentActivity->name != "Boot"', manager)
        self.assertIn('currentActivity->name != "Sleep"', manager)
        self.assertIn("!currentActivity->requiresExclusiveStorageLoop()", manager)
        self.assertNotIn('currentActivity->name == "EpubReader"', manager)
        loop = method_body("src/main.cpp", "void loop() {", "// Under the render lock")
        self.assertIn("activityManager.acceptsControlsLock() && controlsLocked", loop)

    def test_home_header_draws_lock_beside_battery(self) -> None:
        header = method_body("src/components/themes/BaseTheme.cpp", "void BaseTheme::drawHeader(",
                             "void BaseTheme::drawSubHeader(")
        self.assertIn("if (controlsLocked)", header)
        self.assertIn("status.battery.glyphWidth", header)
        self.assertIn("drawStatusIcon(renderer, LockStatusIcon, iconX, iconY)", header)
        rounded = method_body("src/components/themes/roundedraff/RoundedRaffTheme.cpp",
                              "void RoundedRaffTheme::drawHeader(", "void RoundedRaffTheme::drawRecentBookCover(")
        self.assertIn("title == nullptr && !controlsLocked", rounded)

    def test_chapter_boundary_chord_restores_original_page(self) -> None:
        body = method_body("src/activities/reader/EpubReaderActivity.cpp", "bool EpubReaderActivity::restoreChordTurnStart(",
                           "void EpubReaderActivity::onControlsLocked()")
        self.assertIn("currentSpineIndex = chordStartSpineIndex;", body)
        self.assertIn("nextPageNumber = chordStartPage;", body)
        self.assertIn("pendingPageJump.reset();", body)
        queued = method_body("src/activities/reader/EpubReaderActivity.cpp", "if (pendingManualTurn != 0 && !turnGuardActive)",
                             "auto [prevTriggered, nextTriggered, fromTilt, fromSidePress]")
        self.assertIn("if (pendingTurnFromSidePress) captureChordTurnStart();", queued)
        reader = method_body("src/activities/reader/ReaderActivity.cpp", "bool ReaderActivity::revertChordPageTurn(",
                             "void ReaderActivity::render(")
        self.assertIn("restoreChordTurnStart(sidePressTurn.forward)", reader)

    def test_lock_keeps_only_queued_remote_turns(self) -> None:
        body = method_body("src/activities/reader/EpubReaderActivity.cpp", "void EpubReaderActivity::onControlsLocked() {",
                           "void EpubReaderActivity::jumpToPercent(")
        self.assertIn("if (!pendingTurnFromRemote) {", body)
        queued = method_body("src/activities/reader/EpubReaderActivity.cpp", "if (turnGuardActive) {\n    pendingManualTurn =",
                             "if (fromSidePress) captureChordTurnStart();")
        self.assertIn("pendingTurnFromRemote = !fromTilt && mappedInput.hasRemoteClick();", queued)

    def test_touch_and_remote_inputs_are_blocked(self) -> None:
        for start, end in (
            ("bool MappedInputManager::wasScreenTouchPressed(", "bool MappedInputManager::wasScreenTouchDown("),
            ("bool MappedInputManager::wasBackGesture(", "bool MappedInputManager::wasHomeGesture("),
            ("bool MappedInputManager::wasPressed(", "bool MappedInputManager::wasReleased("),
            ("bool MappedInputManager::wasReleased(", "bool MappedInputManager::wasLongPressed("),
        ):
            with self.subTest(start=start):
                body = method_body("src/MappedInputManager.cpp", start, end)
                self.assertIn("if (inputBlocked) return false;", body)


if __name__ == "__main__":
    unittest.main()
