add_executable(logger_test logger_test.cc)

target_include_directories(logger_test PRIVATE ${YACUL_INCLUDE_ROOT})

target_link_libraries(logger_test libbasics liblogging)
