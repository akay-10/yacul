add_library(libconcurrency STATIC lock_id.cc lock_range.cc threadpool.cc)

target_include_directories(libconcurrency PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(libconcurrency PUBLIC libbasics liblogging)

add_library(${PROJECT_NAME}::concurrency ALIAS libconcurrency)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
