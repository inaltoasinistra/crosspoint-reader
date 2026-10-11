#include <gtest/gtest.h>

#include "lib/ButtonChord/ButtonChord.h"

class TestButtonChord : public ::testing::Test {
 protected:
  ButtonChord chord;
};

TEST_F(TestButtonChord, test_single_button_does_nothing) {
  /** One chord button alone neither fires nor holds input. */
  EXPECT_FALSE(chord.update(true, false));
  EXPECT_FALSE(chord.update(false, true));
  EXPECT_FALSE(chord.isHeld());
}

TEST_F(TestButtonChord, test_fires_once_while_held) {
  /** Both buttons down fires on the first poll only, not on every poll they stay held. */
  chord.update(false, true);
  EXPECT_TRUE(chord.update(true, true));
  EXPECT_FALSE(chord.update(true, true));
}

TEST_F(TestButtonChord, test_held_until_both_released) {
  /** After firing, input stays held while either button is still down. */
  chord.update(true, true);
  chord.update(true, false);
  EXPECT_TRUE(chord.isHeld());
  chord.update(false, false);
  EXPECT_TRUE(chord.isHeld());
  chord.update(false, false);
  EXPECT_FALSE(chord.isHeld());
}

TEST_F(TestButtonChord, test_release_poll_still_held) {
  /** The poll that releases both buttons at once is still held, so its release edges are swallowed. */
  chord.update(true, true);
  chord.update(false, false);
  EXPECT_TRUE(chord.isHeld());
  chord.update(false, false);
  EXPECT_FALSE(chord.isHeld());
}

TEST_F(TestButtonChord, test_repress_one_button_fires_again) {
  /** Releasing one button and pressing it again while the other is held is a new chord. */
  chord.update(true, true);
  chord.update(true, false);
  EXPECT_TRUE(chord.update(true, true));
}

TEST_F(TestButtonChord, test_reset_drops_hold) {
  /** reset() forgets a held chord, e.g. when the chord stops applying to the current screen. */
  chord.update(true, true);
  chord.reset();
  EXPECT_FALSE(chord.isHeld());
  EXPECT_TRUE(chord.update(true, true));
}

TEST(TestHoldButtonChord, test_waits_two_seconds_from_second_button) {
  HoldButtonChord chord;
  EXPECT_FALSE(chord.update(true, false, 100));
  EXPECT_FALSE(chord.isHeld());
  EXPECT_FALSE(chord.update(true, true, 1000));
  EXPECT_TRUE(chord.isHeld());
  // The SDK's 650 ms Power event must be suppressed before the lock fires.
  EXPECT_FALSE(chord.update(true, true, 1650));
  EXPECT_TRUE(chord.isHeld());
  EXPECT_FALSE(chord.update(true, true, 2999));
  EXPECT_TRUE(chord.update(true, true, 3000));
  EXPECT_FALSE(chord.update(true, true, 6000));
}

TEST(TestHoldButtonChord, test_interrupted_hold_consumes_staggered_release) {
  HoldButtonChord chord;
  chord.update(true, true, 1000);
  EXPECT_FALSE(chord.update(false, true, 1500));
  // Holding the remaining key must not become Confirm or sleep.
  EXPECT_FALSE(chord.update(false, true, 4000));
  EXPECT_TRUE(chord.isHeld());
  EXPECT_FALSE(chord.update(false, false, 4100));
  EXPECT_TRUE(chord.isHeld());
  chord.update(false, false, 4150);
  EXPECT_FALSE(chord.isHeld());
}

TEST(TestHoldButtonChord, test_repress_does_not_restart_cancelled_hold) {
  HoldButtonChord chord;
  chord.update(true, true, 1000);
  chord.update(true, false, 1500);
  EXPECT_FALSE(chord.update(true, true, 1600));
  EXPECT_FALSE(chord.update(true, true, 5000));
  EXPECT_TRUE(chord.isHeld());
}

TEST(TestHoldButtonChord, test_requires_full_release_before_next_toggle) {
  HoldButtonChord chord;
  chord.update(true, true, 1000);
  EXPECT_TRUE(chord.update(true, true, 3000));
  chord.update(false, true, 3100);
  EXPECT_FALSE(chord.update(true, true, 3200));
  EXPECT_FALSE(chord.update(true, true, 6000));
  chord.update(false, false, 6100);
  EXPECT_TRUE(chord.isHeld());
  chord.update(false, false, 6150);
  EXPECT_FALSE(chord.isHeld());
  chord.update(true, true, 6200);
  EXPECT_TRUE(chord.update(true, true, 8200));
}

TEST(TestHoldButtonChord, test_hold_across_millis_wrap) {
  HoldButtonChord chord;
  chord.update(true, true, UINT32_MAX - 999);
  EXPECT_FALSE(chord.update(true, true, 999));
  EXPECT_TRUE(chord.update(true, true, 1000));
}

TEST(TestChordTurn, test_undo_inside_window) {
  /** A turn recorded just before the chord is undone. */
  ChordTurn turn;
  turn.record(true, 1000);
  EXPECT_TRUE(turn.shouldUndo(1000 + ChordTurn::UNDO_WINDOW_MS));
}

TEST(TestChordTurn, test_no_undo_outside_window) {
  /** A turn older than the window is a deliberate page turn and stays. */
  ChordTurn turn;
  turn.record(true, 1000);
  EXPECT_FALSE(turn.shouldUndo(1000 + ChordTurn::UNDO_WINDOW_MS + 1));
}

TEST(TestChordTurn, test_no_undo_without_turn) {
  /** Nothing to undo when no turn was recorded. */
  const ChordTurn turn;
  EXPECT_FALSE(turn.shouldUndo(0));
}

TEST(TestChordTurn, test_undo_across_millis_wrap) {
  /** The window survives the 32-bit millis() counter wrapping between turn and chord. */
  ChordTurn turn;
  turn.record(false, UINT32_MAX - 50);
  EXPECT_TRUE(turn.shouldUndo(100));
}
