add_library(libqos STATIC qos.cc qos_stats.cc token_bucket.cc)

target_include_directories(libqos PUBLIC ${UTILS_SRC_ROOT})

target_link_libraries(libqos PUBLIC libbasics liblogging libconcurrency)

add_library(${PROJECT_NAME}::qos ALIAS libqos)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
