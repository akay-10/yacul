add_library(libhashing STATIC bloom_filter.cc os_murmur_hash3.cc)

target_include_directories(libhashing PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(libhashing PUBLIC libbasics absl::hash)

add_library(${PROJECT_NAME}::hashing ALIAS libhashing)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
