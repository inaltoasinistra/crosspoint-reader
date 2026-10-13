add_executable(ScreenCodecTest
  ScreenCodecTest.cpp
  ${REPO_ROOT}/lib/ScreenCodec/ScreenCodec.cpp
)

target_link_libraries(ScreenCodecTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ScreenCodecTest)
