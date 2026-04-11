# Backward-cpp for stack traces
option(UTILS_FETCH_BACKWARD "Fetch backward-cpp for stack traces" ON)
option(UTILS_USE_EXTERNAL_BACKWARD "Use externally provided backward-cpp" OFF)

# Check if backward-cpp is available
set(UTILS_BACKWARD_AVAILABLE ON)
set(BACKWARD_SOURCE_DIR "")

if(UTILS_USE_EXTERNAL_BACKWARD)
  # Try to find backward-cpp in system
  find_path(BACKWARD_INCLUDE_DIR "backward.hpp" QUIET)
  if(BACKWARD_INCLUDE_DIR)
    set(BACKWARD_SOURCE_DIR "${BACKWARD_INCLUDE_DIR}")
  else()
    set(UTILS_BACKWARD_AVAILABLE OFF)
    message(
      WARNING
        "backward-cpp not found, logging will have limited stack trace support")
  endif()
elseif(UTILS_FETCH_BACKWARD)
  FetchContent_Declare(
    backward
    GIT_REPOSITORY https://github.com/bombela/backward-cpp.git
    GIT_TAG v1.6)

  FetchContent_MakeAvailable(backward)
  set(BACKWARD_SOURCE_DIR "${backward_SOURCE_DIR}")
endif()

add_library(liblogging STATIC logger.cc)

target_include_directories(
  liblogging PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                    $<INSTALL_INTERFACE:include>)

if(UTILS_BACKWARD_AVAILABLE AND BACKWARD_SOURCE_DIR)
  target_include_directories(liblogging PUBLIC ${BACKWARD_SOURCE_DIR})
endif()

target_link_libraries(
  liblogging
  PUBLIC libbasics
         absl::check
         absl::flags
         absl::flags_parse
         absl::log
         absl::log_initialize
         absl::vlog_is_on
         absl::strings)

find_library(DL_LIB dl)
find_library(BFD_LIB bfd)
find_library(UNWIND_LIB unwind)

if(BFD_LIB AND UTILS_BACKWARD_AVAILABLE)
  target_link_libraries(liblogging PUBLIC bfd)
  target_compile_definitions(liblogging PUBLIC BACKWARD_HAS_BFD=1)
  message(
    STATUS
      "backward-cpp: bfd found, symbol resolution with file/line numbers enabled"
  )
else()
  message(
    WARNING "backward-cpp: bfd not found, no source file/line number resolution"
  )
endif()

if(UNWIND_LIB AND UTILS_BACKWARD_AVAILABLE)
  target_link_libraries(liblogging PUBLIC unwind)
  target_compile_definitions(liblogging PUBLIC BACKWARD_HAS_UNWIND=1)
  message(
    STATUS "backward-cpp: libunwind found, using unwind for stack capture")
else()
  message(
    WARNING
      "backward-cpp: libunwind not found, falling back to backtrace() for stack capture"
  )
endif()

if(DL_LIB)
  target_link_libraries(liblogging PUBLIC dl)
  message(STATUS "backward-cpp: libdl found, symbol name resolution enabled")
else()
  message(
    WARNING
      "backward-cpp: libdl not found, symbol names may be mangled or missing")
endif()

add_library(${PROJECT_NAME}::logging ALIAS liblogging)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
