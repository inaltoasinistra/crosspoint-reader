#pragma once
struct FakeActivityManager {
  const char* currentActivityName() const { return "Home"; }
};
inline FakeActivityManager activityManager;
