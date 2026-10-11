#include <gtest/gtest.h>

#include "lib/TiltGesture/TiltSession.h"

namespace {

constexpr uint8_t TILT_OFF = 0;
constexpr uint8_t TILT_NORMAL = 1;
constexpr uint8_t TILT_INVERTED = 2;

}  // namespace

class TestTiltSession : public ::testing::Test {
 protected:
  TiltSession session;
};

TEST_F(TestTiltSession, test_per_session_starts_disarmed) {
  /** A per-session start gates any configured mode to off until armed. */
  session.begin(true);
  EXPECT_FALSE(session.isEnabled());
  EXPECT_EQ(session.gate(TILT_INVERTED), TILT_OFF);
}

TEST_F(TestTiltSession, test_always_passes_through) {
  /** Outside per-session mode the configured mode is applied unchanged. */
  session.begin(false);
  EXPECT_TRUE(session.isEnabled());
  EXPECT_EQ(session.gate(TILT_INVERTED), TILT_INVERTED);
}

TEST_F(TestTiltSession, test_toggle_arms_and_disarms) {
  /** Each toggle flips the state and reports the new one. */
  session.begin(true);
  EXPECT_TRUE(session.toggle());
  EXPECT_EQ(session.gate(TILT_INVERTED), TILT_INVERTED);
  EXPECT_FALSE(session.toggle());
  EXPECT_EQ(session.gate(TILT_INVERTED), TILT_OFF);
}

TEST_F(TestTiltSession, test_is_active_follows_mode_and_session) {
  /** Tilt is live only with a configured mode and an enabled session. */
  session.begin(false);
  EXPECT_TRUE(session.isActive(TILT_INVERTED));
  EXPECT_FALSE(session.isActive(TILT_OFF));
  session.begin(true);
  EXPECT_FALSE(session.isActive(TILT_INVERTED));
  session.toggle();
  EXPECT_TRUE(session.isActive(TILT_INVERTED));
}

TEST_F(TestTiltSession, test_always_toggle_pauses) {
  /** Under "Always" a toggle pauses tilt and a second toggle resumes it. */
  session.begin(false);
  EXPECT_FALSE(session.toggle());
  EXPECT_FALSE(session.isActive(TILT_INVERTED));
  EXPECT_EQ(session.gate(TILT_INVERTED), TILT_OFF);
  EXPECT_TRUE(session.toggle());
  EXPECT_TRUE(session.isActive(TILT_INVERTED));
}

TEST_F(TestTiltSession, test_settings_pick_arms_per_session) {
  /** Picking a direction in settings arms a per-session start; the stored value seen first does not. */
  session.begin(true);
  EXPECT_FALSE(session.observeMode(TILT_NORMAL));
  EXPECT_FALSE(session.isActive(TILT_NORMAL));
  EXPECT_TRUE(session.observeMode(TILT_INVERTED));
  EXPECT_TRUE(session.isActive(TILT_INVERTED));
}

TEST_F(TestTiltSession, test_settings_off_does_not_arm) {
  /** Switching the mode to off is not a pick and leaves the session disarmed. */
  session.begin(true);
  session.observeMode(TILT_NORMAL);
  EXPECT_FALSE(session.observeMode(TILT_OFF));
  EXPECT_FALSE(session.isEnabled());
}

TEST_F(TestTiltSession, test_settings_pick_keeps_always_pause) {
  /** Under "Always" a settings change does not undo a long-press pause. */
  session.begin(false);
  session.observeMode(TILT_NORMAL);
  session.toggle();
  EXPECT_FALSE(session.observeMode(TILT_INVERTED));
  EXPECT_FALSE(session.isEnabled());
}

TEST_F(TestTiltSession, test_activation_change_stays_live) {
  /** Switching to per-session activation while running keeps tilt live until sleep. */
  session.begin(false);
  session.observeMode(TILT_NORMAL);
  session.changeActivation(true);
  EXPECT_TRUE(session.isActive(TILT_NORMAL));
  session.toggle();
  EXPECT_FALSE(session.isActive(TILT_NORMAL));
}

TEST_F(TestTiltSession, test_begin_resets_arm) {
  /** Re-running begin() drops an arm made in the previous configuration. */
  session.begin(true);
  session.toggle();
  session.begin(true);
  EXPECT_FALSE(session.isEnabled());
}
