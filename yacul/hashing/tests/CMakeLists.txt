add_executable(os_murmur_hash3_test os_murmur_hash3_test.cc)

target_include_directories(os_murmur_hash3_test PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(os_murmur_hash3_test libhashing GTest::gtest_main)

add_executable(hash_test hash_test.cc)

target_include_directories(hash_test PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(hash_test libhashing GTest::gtest_main)
