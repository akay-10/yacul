add_executable(qos_test qos_test.cc)

target_include_directories(qos_test PRIVATE ${UTILS_SRC_ROOT}/qos)

target_link_libraries(qos_test libqos GTest::gtest_main)
