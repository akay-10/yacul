add_executable(basic_test basic_test.cc)

target_include_directories(basic_test PRIVATE ${YACUL_INCLUDE_ROOT})

target_link_libraries(basic_test libbasics GTest::gtest_main)
