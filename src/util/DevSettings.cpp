#include "DevSettings.h"

#ifdef SERIAL_TEST_HOOKS
void DevSettings::toJson(JsonDocument& doc) const { doc["serial_control"] = serialControl; }

bool DevSettings::fromJson(JsonVariantConst doc) {
  serialControl = doc["serial_control"].is<bool>() && doc["serial_control"].as<bool>();
  return true;
}
#endif
