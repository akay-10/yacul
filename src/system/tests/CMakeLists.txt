add_executable(popen_wrapper_test popen_wrapper_test.cc)

target_include_directories(popen_wrapper_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/src/system)

target_link_libraries(popen_wrapper_test libsystem GTest::gtest_main)

add_executable(system_info_test system_info_test.cc)

target_include_directories(system_info_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/src/system)

target_link_libraries(system_info_test libsystem GTest::gtest_main)

