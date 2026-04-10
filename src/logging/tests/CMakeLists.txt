add_executable(logger_test logger_test.cc)

target_include_directories(logger_test PRIVATE ${UTILS_SRC_ROOT}/logging)

target_link_libraries(logger_test libbasics liblogging)
