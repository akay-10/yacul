add_library(libmisc INTERFACE)

target_include_directories(libmisc INTERFACE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(libmisc INTERFACE libbasics)

add_library(${PROJECT_NAME}::misc ALIAS libmisc)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()
