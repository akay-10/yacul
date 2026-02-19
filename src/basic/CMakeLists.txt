add_library(
  libbasics INTERFACE
)

target_include_directories(
  libbasics  INTERFACE
  ${CMAKE_SOURCE_DIR}/src/basic
)

if(BUILD_TEST)
  add_subdirectory(tests)
endif()

