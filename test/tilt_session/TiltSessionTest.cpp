#include <gtest/gtest.h>

#include "lib/TiltGesture/TiltSession.h"

namespace {

constexpr uint8_t TILT_OFF = 0;
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

TEST_F(TestTiltSession, test_begin_resets_arm) {
  /** Re-running begin() drops an arm made in the previous configuration. */
  session.begin(true);
  session.toggle();
  session.begin(true);
  EXPECT_FALSE(session.isEnabled());
}
