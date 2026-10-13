#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <PersistableStore.h>
#include <gtest/gtest.h>

#include "util/DevSettings.h"
#include "util/SerialCommands.h"

namespace {
std::string savedJson;
int writes = 0;
bool saveSucceeds = true;

std::string command(const char* text) {
  logSerial.input = std::string("CMD:") + text + "\n";
  logSerial.output.clear();
  SerialCommands::poll(GfxRenderer{});
  return logSerial.output;
}
}  // namespace

// Use the real store's JSON and locking code with an in-memory file boundary.
bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  EXPECT_STREQ(path, "/.crosspoint/dev.json");
  ++writes;
  if (!saveSucceeds) return false;
  savedJson.clear();
  serializeJson(doc, savedJson);
  return true;
}
bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  EXPECT_STREQ(path, "/.crosspoint/dev.json");
  return !savedJson.empty() && !deserializeJson(doc, savedJson);
}

#ifdef SERIAL_TEST_HOOKS
TEST(SerialCommands, PersistentControlAndSleep) {
  SerialCommands::begin();
  auto& settings = DevSettings::getInstance();
  EXPECT_FALSE(settings.loadFromFile());
  EXPECT_FALSE(settings.serialControl);
  EXPECT_EQ(command("DEV"), "DEV serial_control=0\nOK DEV\n");
  for (const char* cmd : {"PRESS back", "HOLD back", "RELEASE all"})
    EXPECT_NE(command(cmd).find("disabled"), std::string::npos);
  EXPECT_FALSE(SerialCommands::hostKeepsAwake());
  EXPECT_NE(command("INFO").find("inject=0"), std::string::npos);
  for (const char* cmd :
       {"DEV unknown on", "DEV serial_control", "DEV serial_control yes", "DEV serial_control on extra"})
    EXPECT_EQ(command(cmd), "ERR DEV bad-args\n");
  EXPECT_EQ(writes, 0);

  EXPECT_EQ(command("DEV serial_control on"), "OK DEV\n");
  EXPECT_EQ(writes, 1);
  settings.serialControl = false;
  EXPECT_TRUE(settings.loadFromFile());
  EXPECT_TRUE(settings.serialControl);
  EXPECT_TRUE(SerialCommands::hostKeepsAwake());
  logSerial.writeLatencyMs = 700;
  EXPECT_NE(command("SCREENSHOT").find("OK SCREENSHOT\n"), std::string::npos);
  logSerial.writeLatencyMs = 0;
  EXPECT_GT(nowMs, 2000u);
  EXPECT_TRUE(SerialCommands::hostKeepsAwake());
  EXPECT_NE(command("INFO").find("inject=1"), std::string::npos);
  EXPECT_EQ(command("DEV serial_control on"), "OK DEV\n");
  EXPECT_EQ(writes, 1);
  EXPECT_EQ(command("HOLD back"), "OK HOLD\n");
  ASSERT_NE(HalGPIO::injected, nullptr);
  EXPECT_NE(HalGPIO::injected(), 0);
  EXPECT_EQ(command("PRESS down"), "OK PRESS\n");
  EXPECT_EQ(command("DEV serial_control off"), "OK DEV\n");
  EXPECT_EQ(HalGPIO::injected(), 0);
  EXPECT_FALSE(SerialCommands::hostKeepsAwake());
  EXPECT_TRUE(SerialCommands::hostLeft());
  EXPECT_FALSE(SerialCommands::hostLeft());
  settings.serialControl = true;
  EXPECT_TRUE(settings.loadFromFile());
  EXPECT_FALSE(settings.serialControl);
  EXPECT_EQ(writes, 2);

  saveSucceeds = false;
  EXPECT_EQ(command("DEV serial_control on"), "ERR DEV save-failed\n");
  EXPECT_FALSE(settings.serialControl);
  saveSucceeds = true;
  for (const char* json : {"{}", "{\"serial_control\":\"true\"}", "{\"serial_control\":1}"}) {
    savedJson = json;
    settings.serialControl = true;
    EXPECT_TRUE(settings.loadFromFile());
    EXPECT_FALSE(settings.serialControl);
  }
}

TEST(SerialCommands, ScreenshotsAreRleOnly) {
  EXPECT_EQ(command("SCREENSHOT raw"), "ERR SCREENSHOT bad-args\n");
  EXPECT_EQ(command("SCREENSHOT rle"), "ERR SCREENSHOT bad-args\n");
  const auto shot = command("SCREENSHOT");
  EXPECT_EQ(shot.find("SCREENSHOT_START:6:16:2:rle\n"), 0u);
  EXPECT_NE(shot.find("OK SCREENSHOT\n"), std::string::npos);
}
#else
// Release builds compile the module out: serial input is ignored entirely.
TEST(SerialCommands, ReleaseIgnoresCommands) {
  SerialCommands::begin();
  for (const char* cmd : {"DEV serial_control on", "PRESS back", "SCREENSHOT", "INFO"}) EXPECT_EQ(command(cmd), "");
  EXPECT_FALSE(SerialCommands::hostKeepsAwake());
  EXPECT_FALSE(SerialCommands::hostLeft());
  EXPECT_EQ(HalGPIO::injected, nullptr);
  EXPECT_EQ(writes, 0);
}
#endif
