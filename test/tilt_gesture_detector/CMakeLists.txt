add_executable(TiltGestureDetectorTest
  TiltGestureDetectorTest.cpp
  ${REPO_ROOT}/lib/TiltGesture/TiltGestureDetector.cpp
)

target_link_libraries(TiltGestureDetectorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TiltGestureDetectorTest)
