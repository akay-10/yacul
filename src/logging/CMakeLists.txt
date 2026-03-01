FetchContent_Declare(
  backward
  GIT_REPOSITORY https://github.com/bombela/backward-cpp.git
  GIT_TAG v1.6)

FetchContent_MakeAvailable(backward)

add_library(liblogging STATIC logger.cc)

target_include_directories(liblogging PUBLIC ${CMAKE_SOURCE_DIR}/src
                                             ${backward_SOURCE_DIR})

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

if(BFD_LIB)
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

if(UNWIND_LIB)
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

if(BUILD_TEST)
  add_subdirectory(tests)
endif()
