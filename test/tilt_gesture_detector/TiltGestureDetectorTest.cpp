#include <gtest/gtest.h>

#include "lib/TiltGesture/TiltGestureDetector.h"

namespace {

constexpr float REST_G = 1.0f;
constexpr float BUMP_G = 1.8f;  // Linear acceleration well outside the tolerance
constexpr float FLICK_DPS = 300.0f;
constexpr float RETURN_DPS = -250.0f;
constexpr float STILL_DPS = 10.0f;
constexpr uint32_t START_MS = 1000;  // Past the cooldown started by reset(0)

}  // namespace

class TestTiltGestureDetector : public ::testing::Test {
 protected:
  TiltGestureDetector detector;

  void SetUp() override { detector.reset(0); }

  TiltResult feed(const TiltDetectionMode mode, const float dps, const float accelG, const uint32_t ms) {
    return detector.update(mode, {dps, accelG, ms});
  }
};

TEST_F(TestTiltGestureDetector, test_rate_fires_on_crossing) {
  /** RATE fires on the first sample past the threshold, in either direction. */
  const TiltResult forward = feed(TiltDetectionMode::RATE, FLICK_DPS, REST_G, START_MS);
  EXPECT_TRUE(forward.fired);
  EXPECT_TRUE(forward.forward);

  feed(TiltDetectionMode::RATE, STILL_DPS, REST_G, START_MS + 50);
  const TiltResult back = feed(TiltDetectionMode::RATE, -FLICK_DPS, REST_G, START_MS + 1000);
  EXPECT_TRUE(back.fired);
  EXPECT_FALSE(back.forward);
}

TEST_F(TestTiltGestureDetector, test_rate_ignores_accel) {
  /** RATE keeps the original behaviour: linear acceleration does not block a turn. */
  EXPECT_TRUE(feed(TiltDetectionMode::RATE, FLICK_DPS, BUMP_G, START_MS).fired);
}

TEST_F(TestTiltGestureDetector, test_rate_waits_for_neutral) {
  /** After firing, a rotation that never slows below the neutral rate cannot fire again. */
  EXPECT_TRUE(feed(TiltDetectionMode::RATE, FLICK_DPS, REST_G, START_MS).fired);
  EXPECT_FALSE(feed(TiltDetectionMode::RATE, FLICK_DPS, REST_G, START_MS + 1000).fired);
  EXPECT_FALSE(feed(TiltDetectionMode::RATE, FLICK_DPS, REST_G, START_MS + 1050).fired);

  feed(TiltDetectionMode::RATE, STILL_DPS, REST_G, START_MS + 1100);
  EXPECT_TRUE(feed(TiltDetectionMode::RATE, FLICK_DPS, REST_G, START_MS + 1150).fired);
}

TEST_F(TestTiltGestureDetector, test_rate_cooldown) {
  /** A second crossing within the cooldown window does not fire. */
  EXPECT_TRUE(feed(TiltDetectionMode::RATE, FLICK_DPS, REST_G, START_MS).fired);
  feed(TiltDetectionMode::RATE, STILL_DPS, REST_G, START_MS + 50);
  EXPECT_FALSE(feed(TiltDetectionMode::RATE, FLICK_DPS, REST_G, START_MS + 300).fired);
}

TEST_F(TestTiltGestureDetector, test_accel_rejects_bump) {
  /** ACCEL ignores a crossing while the device is linearly accelerated, but still reports activity. */
  const TiltResult result = feed(TiltDetectionMode::ACCEL, FLICK_DPS, BUMP_G, START_MS);
  EXPECT_FALSE(result.fired);
  EXPECT_TRUE(result.activity);
}

TEST_F(TestTiltGestureDetector, test_accel_fires_on_rotation) {
  /** ACCEL fires immediately on a pure rotation. */
  const TiltResult result = feed(TiltDetectionMode::ACCEL, -FLICK_DPS, REST_G, START_MS);
  EXPECT_TRUE(result.fired);
  EXPECT_FALSE(result.forward);
}

TEST_F(TestTiltGestureDetector, test_return_fires_after_return) {
  /** ACCEL_RETURN only fires on the return stroke, with the out-stroke direction. */
  EXPECT_FALSE(feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS).fired);
  const TiltResult result = feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, REST_G, START_MS + 200);
  EXPECT_TRUE(result.fired);
  EXPECT_TRUE(result.forward);
}

TEST_F(TestTiltGestureDetector, test_return_backward) {
  /** A backward out-stroke followed by a positive return turns back. */
  feed(TiltDetectionMode::ACCEL_RETURN, -FLICK_DPS, REST_G, START_MS);
  const TiltResult result = feed(TiltDetectionMode::ACCEL_RETURN, -RETURN_DPS, REST_G, START_MS + 200);
  EXPECT_TRUE(result.fired);
  EXPECT_FALSE(result.forward);
}

TEST_F(TestTiltGestureDetector, test_return_same_direction) {
  /** Continuing to rotate in the out-stroke direction is not a return. */
  feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS);
  EXPECT_FALSE(feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS + 200).fired);
}

TEST_F(TestTiltGestureDetector, test_return_timeout) {
  /** A return stroke after the window has expired does not complete the flick. */
  feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS);
  EXPECT_FALSE(feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, REST_G, START_MS + 800).fired);
}

TEST_F(TestTiltGestureDetector, test_return_bump_on_out_stroke) {
  /** An out-stroke with linear acceleration does not arm the gesture. */
  feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, BUMP_G, START_MS);
  EXPECT_FALSE(feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, REST_G, START_MS + 200).fired);
}

TEST_F(TestTiltGestureDetector, test_return_bump_on_return) {
  /** A return stroke with linear acceleration does not fire. */
  feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS);
  EXPECT_FALSE(feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, BUMP_G, START_MS + 200).fired);
}

TEST_F(TestTiltGestureDetector, test_return_too_early) {
  /** A return reading right after arming is ignored, but a later one in the window fires. */
  feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS);
  EXPECT_FALSE(feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, REST_G, START_MS + 20).fired);
  EXPECT_TRUE(feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, REST_G, START_MS + 100).fired);
}

TEST_F(TestTiltGestureDetector, test_return_cooldown) {
  /** A complete second flick inside the cooldown window does not fire. */
  feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS);
  EXPECT_TRUE(feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, REST_G, START_MS + 200).fired);
  feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS + 300);
  EXPECT_FALSE(feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, REST_G, START_MS + 500).fired);
}

TEST_F(TestTiltGestureDetector, test_reset_drops_arm) {
  /** reset() discards an armed out-stroke so it cannot complete later. */
  feed(TiltDetectionMode::ACCEL_RETURN, FLICK_DPS, REST_G, START_MS);
  detector.reset(START_MS - 600);
  EXPECT_FALSE(feed(TiltDetectionMode::ACCEL_RETURN, RETURN_DPS, REST_G, START_MS + 200).fired);
}
