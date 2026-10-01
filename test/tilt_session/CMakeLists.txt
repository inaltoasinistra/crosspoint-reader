add_executable(TiltSessionTest
  TiltSessionTest.cpp
)

target_link_libraries(TiltSessionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TiltSessionTest)
