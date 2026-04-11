# UTILS_SRC_ROOT is this yacul/ tree. For #include "yacul/..." the compiler
# needs the parent of yacul/ on the path — not CMAKE_SOURCE_DIR, which is the
# top-level project when this tree is add_subdirectory'd from another repo.
set(UTILS_SRC_ROOT "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(YACUL_INCLUDE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_subdirectory(basic)
add_subdirectory(concurrency)
add_subdirectory(hashing)
add_subdirectory(logging)
add_subdirectory(memory)
add_subdirectory(misc)
add_subdirectory(qos)
add_subdirectory(system)
add_subdirectory(transformation)
add_subdirectory(wal)
