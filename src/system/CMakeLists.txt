add_library(libsystem STATIC popen_wrapper.cc system_info.cc)

target_include_directories(libsystem PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(libsystem PUBLIC libbasics)

add_library(${PROJECT_NAME}::system ALIAS libsystem)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
