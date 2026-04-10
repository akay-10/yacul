add_library(libbasics INTERFACE)

target_include_directories(libbasics INTERFACE ${UTILS_SRC_ROOT}/basic)

add_library(${PROJECT_NAME}::basics ALIAS libbasics)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
