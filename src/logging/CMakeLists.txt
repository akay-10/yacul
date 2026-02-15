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
  dl
  bfd
)

add_subdirectory(tests)
