add_executable(
  basic_test
  basic_test.cc
)
target_include_directories(
  basic_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src/basic
)
target_link_libraries(
  basic_test
  libbasics
)

