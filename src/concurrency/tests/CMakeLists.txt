add_executable(threadpool_test threadpool_test.cc)

target_include_directories(threadpool_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/src/concurrency)

target_link_libraries(threadpool_test libconcurrency GTest::gtest_main)
