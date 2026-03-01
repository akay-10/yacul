add_executable(os_usage_test os_usage_test.cc)

target_include_directories(os_usage_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/src/hashing)

target_link_libraries(os_usage_test libhashing GTest::gtest_main)

add_executable(hash_test hash_test.cc)

target_include_directories(hash_test PRIVATE ${CMAKE_SOURCE_DIR}/src/hashing)

target_link_libraries(hash_test libhashing GTest::gtest_main)
