add_library(libqos STATIC qos.cc qos_stats.cc token_bucket.cc)

target_include_directories(libqos PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                                         $<INSTALL_INTERFACE:include>)

target_link_libraries(libqos PUBLIC libbasics liblogging libconcurrency)

add_library(${PROJECT_NAME}::qos ALIAS libqos)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
