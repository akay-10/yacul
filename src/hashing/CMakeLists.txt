add_library(
  libhashing STATIC
  bloom_filter.cc
  os_murmur_hash3.cc
)

target_include_directories(
  libhashing  PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(
  libhashing
  libbasics
  absl::hash
)

if(BUILD_TEST)
  add_subdirectory(tests)
endif()

