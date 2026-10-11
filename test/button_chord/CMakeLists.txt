add_executable(ButtonChordTest
  ButtonChordTest.cpp
)

target_link_libraries(ButtonChordTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ButtonChordTest)
