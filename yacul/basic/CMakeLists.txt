add_library(libbasics INTERFACE)

target_include_directories(
  libbasics INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                      $<INSTALL_INTERFACE:include>)

add_library(${PROJECT_NAME}::basics ALIAS libbasics)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
