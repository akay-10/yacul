add_executable(scoped_exec_test scoped_exec_test.cc)

target_include_directories(scoped_exec_test PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(scoped_exec_test libmisc GTest::gtest_main)

add_executable(monitor_process_test monitor_process_test.cc)

target_include_directories(monitor_process_test PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(monitor_process_test libmisc GTest::gtest_main)
