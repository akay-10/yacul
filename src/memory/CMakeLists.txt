# Options for gperftools (tcmalloc)
option(UTILS_FETCH_GPERFTOOLS "Fetch gperftools for tcmalloc" ON)
option(UTILS_USE_EXTERNAL_GPERFTOOLS "Use externally provided gperftools" OFF)

set(UTILS_GPERFTOOLS_AVAILABLE ON)

if(UTILS_USE_EXTERNAL_GPERFTOOLS)
  find_path(GPERFTOOLS_INCLUDE_DIR "gperftools/heap-checker.h" QUIET)
  if(NOT GPERFTOOLS_INCLUDE_DIR)
    set(UTILS_GPERFTOOLS_AVAILABLE OFF)
    message(
      WARNING
        "gperftools not found, memory module will be built without tcmalloc")
  endif()
elseif(UTILS_FETCH_GPERFTOOLS)
  FetchContent_Declare(
    gperftools
    GIT_REPOSITORY https://github.com/gperftools/gperftools.git
    GIT_TAG gperftools-2.18)

  set(BUILD_TESTING OFF)

  FetchContent_MakeAvailable(gperftools)
endif()

add_library(libmemory STATIC arena.cc buffer.cc)

target_include_directories(libmemory PUBLIC ${UTILS_SRC_ROOT})

if(UTILS_GPERFTOOLS_AVAILABLE)
  if(UTILS_USE_EXTERNAL_GPERFTOOLS)
    target_include_directories(libmemory PUBLIC ${GPERFTOOLS_INCLUDE_DIR})
    find_library(TCMALLOC_LIB tcmalloc QUIET)
  else()
    target_include_directories(libmemory PUBLIC ${gperftools_SOURCE_DIR}/src)
    set(TCMALLOC_LIB tcmalloc)
  endif()

  if(TCMALLOC_LIB)
    target_link_libraries(libmemory PUBLIC ${TCMALLOC_LIB})
  else()
    message(
      WARNING "tcmalloc library not found, memory module will work without it")
  endif()
endif()

target_link_libraries(libmemory PUBLIC libbasics liblogging)

add_library(${PROJECT_NAME}::memory ALIAS libmemory)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
