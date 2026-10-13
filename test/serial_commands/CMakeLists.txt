find_path(SERIAL_TEST_JSON_INCLUDE ArduinoJson.h
  PATHS ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src
        ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src
  NO_DEFAULT_PATH)
if(NOT SERIAL_TEST_JSON_INCLUDE)
  FetchContent_Declare(arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2)
  FetchContent_MakeAvailable(arduinojson)
  set(SERIAL_TEST_JSON_INCLUDE ${arduinojson_SOURCE_DIR}/src)
endif()

foreach(variant IN ITEMS Dev Release)
  add_executable(SerialCommands${variant}Test
    SerialCommandsTest.cpp
    ${REPO_ROOT}/src/util/SerialCommands.cpp
    ${REPO_ROOT}/src/util/DevSettings.cpp
    ${REPO_ROOT}/lib/ScreenCodec/ScreenCodec.cpp)
  target_include_directories(SerialCommands${variant}Test PRIVATE
    stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/ScreenCodec
    ${REPO_ROOT}/lib/Serialization ${SERIAL_TEST_JSON_INCLUDE})
  target_compile_definitions(SerialCommands${variant}Test PRIVATE ARDUINO_USB_CDC_ON_BOOT=1)
  if(variant STREQUAL "Dev")
    target_compile_definitions(SerialCommands${variant}Test PRIVATE SERIAL_TEST_HOOKS)
  endif()
  target_link_libraries(SerialCommands${variant}Test PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(SerialCommands${variant}Test TEST_PREFIX "${variant}.")
endforeach()
