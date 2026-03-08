add_executable(scoped_exec_test scoped_exec_test.cc)

target_include_directories(scoped_exec_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/src/misc)

target_link_libraries(scoped_exec_test libmisc GTest::gtest_main)
