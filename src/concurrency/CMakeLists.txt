add_library(libconcurrency STATIC lock_id.cc threadpool.cc)

target_include_directories(libconcurrency PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(libconcurrency libbasics liblogging)

if(BUILD_TEST)
  add_subdirectory(tests)
endif()
