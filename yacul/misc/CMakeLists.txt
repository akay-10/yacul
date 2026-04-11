add_library(libmisc STATIC monitor_process.cc)

target_include_directories(libmisc PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                                          $<INSTALL_INTERFACE:include>)

target_link_libraries(libmisc PUBLIC libbasics liblogging)

add_library(${PROJECT_NAME}::misc ALIAS libmisc)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
