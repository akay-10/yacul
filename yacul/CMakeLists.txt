# UTILS_SRC_ROOT is yacul/. The compile include root is CMAKE_SOURCE_DIR so
# consumers and this tree include headers as yacul/logging/logger.h, etc.
set(UTILS_SRC_ROOT "${CMAKE_CURRENT_LIST_DIR}")

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
