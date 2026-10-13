#pragma once

#ifdef SERIAL_TEST_HOOKS
#include <PersistableStore.h>

class DevSettings : public PersistableStore<DevSettings> {
  DevSettings() = default;
  friend class PersistableStore<DevSettings>;

 public:
  bool serialControl = false;

  static const char* getFilePath() { return "/.crosspoint/dev.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};
#endif
