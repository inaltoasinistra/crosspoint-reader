#include <gtest/gtest.h>

#include <cstdint>

#include "src/util/RecentActivityWindow.h"

namespace {

constexpr uint32_t WINDOW_MS = 1000;

class TestRecentActivityWindow : public ::testing::Test {};

TEST_F(TestRecentActivityWindow, test_inactive_before_first_mark) {
  // Right after boot millis() is near 0, which must not count as "recent".
  const RecentActivityWindow window(WINDOW_MS);
  EXPECT_FALSE(window.isActive(0));
  EXPECT_FALSE(window.isActive(10));
}

TEST_F(TestRecentActivityWindow, test_window_edges) {
  RecentActivityWindow window(WINDOW_MS);
  window.mark(5000);
  EXPECT_TRUE(window.isActive(5000));
  EXPECT_TRUE(window.isActive(5000 + WINDOW_MS - 1));
  EXPECT_FALSE(window.isActive(5000 + WINDOW_MS));
}

TEST_F(TestRecentActivityWindow, test_mark_extends_window) {
  RecentActivityWindow window(WINDOW_MS);
  window.mark(0);
  window.mark(900);
  EXPECT_TRUE(window.isActive(1500));
}

TEST_F(TestRecentActivityWindow, test_millis_wraparound) {
  // millis() wraps after ~49.7 days; a mark just before the wrap stays active across it.
  RecentActivityWindow window(WINDOW_MS);
  window.mark(UINT32_MAX - 100);
  EXPECT_TRUE(window.isActive(UINT32_MAX - 50));
  EXPECT_TRUE(window.isActive(200));
  EXPECT_FALSE(window.isActive(WINDOW_MS));
}

}  // namespace
