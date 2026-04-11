add_library(libconcurrency STATIC lock_id.cc lock_range.cc spinlock.cc
                                  threadpool.cc)

target_include_directories(
  libconcurrency PUBLIC $<BUILD_INTERFACE:${YACUL_INCLUDE_ROOT}>
                        $<INSTALL_INTERFACE:include>)

target_link_libraries(libconcurrency PUBLIC libbasics liblogging)

add_library(${PROJECT_NAME}::concurrency ALIAS libconcurrency)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
