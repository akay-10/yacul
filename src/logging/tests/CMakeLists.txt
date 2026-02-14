add_executable(
  logger_test
  logger_test.cc
)
target_include_directories(
  logger_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src/logging
)
target_link_libraries(
  logger_test
  libbasics
  liblogging
)
# Ensure debug symbols
target_compile_options(logger_test PRIVATE 
  $<$<CONFIG:Debug>:-g -O0>
  $<$<CONFIG:RelWithDebInfo>:-g -O2>
)
