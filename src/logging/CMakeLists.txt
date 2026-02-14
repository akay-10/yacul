add_library(
  liblogging STATIC
  logger.cc
)

target_include_directories(
  liblogging PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${backward_SOURCE_DIR}
)

target_link_libraries(
  liblogging PUBLIC
  libbasics
  absl::check
  absl::failure_signal_handler
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_globals
  absl::log_initialize
  absl::log_internal_message
  absl::stacktrace
  absl::strings
  absl::str_format
  absl::symbolize
  absl::vlog_is_on
)

# Enable backward-cpp's features
if(TARGET Backward::Backward)
  # Link backward-cpp properly
  target_link_libraries(liblogging PUBLIC Backward::Backward)

  # Add compile definitions for backward-cpp
  target_compile_definitions(liblogging PUBLIC
    BACKWARD_HAS_DW=1
    BACKWARD_HAS_BFD=1
    BACKWARD_HAS_DWARF=1
  )

  # Link against debug symbol libraries for better stack traces
  if(UNIX AND NOT APPLE)
    # Try to find and link libdw
    find_library(DW_LIBRARY dw)
    if(DW_LIBRARY)
      target_link_libraries(liblogging PUBLIC ${DW_LIBRARY})
      message(STATUS "Found libdw: ${DW_LIBRARY}")
    else()
      message(WARNING "libdw not found - stack traces may not have symbols")
    endif()

    # Try to find and link libbfd (best option)
    find_library(BFD_LIBRARY bfd)
    if(BFD_LIBRARY)
      target_link_libraries(liblogging PUBLIC ${BFD_LIBRARY})
      message(STATUS "Found libbfd: ${BFD_LIBRARY}")
    else()
      message(WARNING "libbfd not found - stack traces may not have symbols")
    endif()

    # libiberty is required for libbfd
    find_library(IBERTY_LIBRARY iberty)
    if(IBERTY_LIBRARY)
      target_link_libraries(liblogging PUBLIC ${IBERTY_LIBRARY})
    endif()

    # Link dl for dynamic loading
    target_link_libraries(liblogging PUBLIC ${CMAKE_DL_LIBS})
  endif()
elseif(TARGET Backward)
  # Fallback for older backward-cpp versions
  target_link_libraries(liblogging PUBLIC Backward)

  target_compile_definitions(liblogging PUBLIC
    BACKWARD_HAS_DW=1
    BACKWARD_HAS_BFD=1
    BACKWARD_HAS_DWARF=1
  )

  if(UNIX AND NOT APPLE)
    find_library(DW_LIBRARY dw)
    if(DW_LIBRARY)
      target_link_libraries(liblogging PUBLIC ${DW_LIBRARY})
    endif()

    find_library(BFD_LIBRARY bfd)
    if(BFD_LIBRARY)
      target_link_libraries(liblogging PUBLIC ${BFD_LIBRARY})
    endif()

    find_library(IBERTY_LIBRARY iberty)
    if(IBERTY_LIBRARY)
      target_link_libraries(liblogging PUBLIC ${IBERTY_LIBRARY})
    endif()

    target_link_libraries(liblogging PUBLIC ${CMAKE_DL_LIBS})
  endif()
else()
  message(WARNING "Backward-cpp not found - signal handlers will not be available")
endif()

add_subdirectory(tests)
