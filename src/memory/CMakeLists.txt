FetchContent_Declare(
  gperftools
  GIT_REPOSITORY https://github.com/gperftools/gperftools.git
  GIT_TAG gperftools-2.18)

set(BUILD_TESTING OFF)

FetchContent_MakeAvailable(gperftools)

add_library(libmemory STATIC arena.cc buffer.cc)

target_include_directories(libmemory PUBLIC ${CMAKE_SOURCE_DIR}/src
                                            ${gperftools_SOURCE_DIR}/src)

target_link_libraries(libmemory PUBLIC libbasics liblogging tcmalloc)

if(BUILD_TEST)
  add_subdirectory(tests)
endif()
