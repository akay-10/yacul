add_library(libbasics INTERFACE)

target_include_directories(libbasics INTERFACE ${CMAKE_SOURCE_DIR}/src/basic)

add_library(${PROJECT_NAME}::basics ALIAS libbasics)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
