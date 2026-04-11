add_executable(logger_test logger_test.cc)

target_include_directories(logger_test PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(logger_test libbasics liblogging)
