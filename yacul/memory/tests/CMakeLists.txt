add_executable(tcmalloc_test tcmalloc_test.cc)

target_include_directories(tcmalloc_test PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(tcmalloc_test libmemory GTest::gtest_main)

add_executable(arena_test arena_test.cc)

target_include_directories(arena_test PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(arena_test libmemory GTest::gtest_main)

add_executable(buffer_test buffer_test.cc)

target_include_directories(buffer_test PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(buffer_test libmemory GTest::gtest_main)
