add_library(libmisc STATIC monitor_process.cc)

target_include_directories(libmisc PUBLIC ${UTILS_SRC_ROOT})

target_link_libraries(libmisc PUBLIC libbasics liblogging)

add_library(${PROJECT_NAME}::misc ALIAS libmisc)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
