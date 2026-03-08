set(_DT_TEST_SOURCES
  transformation_test.cc
  compressors_test.cc
  checksummers_test.cc
  hashers_test.cc
  factory_test.cc
)

add_executable(transformation_test ${_DT_TEST_SOURCES})

target_link_libraries(transformation_test
  PRIVATE
    libtransformation
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

target_compile_features(transformation_test PRIVATE cxx_std_20)
