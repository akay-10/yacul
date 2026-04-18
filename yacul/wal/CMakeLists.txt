add_library(libwal STATIC wal.cc)

target_include_directories(
  libwal PUBLIC $<BUILD_INTERFACE:${YACUL_INCLUDE_ROOT}>
                $<INSTALL_INTERFACE:include>)

target_link_libraries(libwal PUBLIC libbasics liblogging)

add_library(${PROJECT_NAME}::wal ALIAS libwal)

if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()

option(UTILS_WAL_ENABLE_PROTOBUF
       "Enable protobuf support (fetched via FetchContent)" ON)

option(UTILS_WAL_ENABLE_FLATBUFFERS
       "Enable FlatBuffers support (fetched via FetchContent)" ON)

# -- Protobuf ---------------------------------------------------------------
if(UTILS_WAL_ENABLE_PROTOBUF)
  message(STATUS "Fetching Protobuf ...")
  FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v25.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake)
  set(protobuf_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE)
  set(protobuf_BUILD_SHARED_LIBS
      OFF
      CACHE BOOL "" FORCE)
  set(protobuf_WITH_ZLIB
      OFF
      CACHE BOOL "" FORCE)
  set(ABSL_PROPAGATE_CXX_STD
      ON
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(protobuf)

  target_link_libraries(libwal PUBLIC protobuf)
endif()

# -- FlatBuffers -----------------------------------------------------------
if(UTILS_WAL_ENABLE_FLATBUFFERS)
  message(STATUS "Fetching FlatBuffers ...")
  FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE)
  set(FLATBUFFERS_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE)
  set(FLATBUFFERS_BUILD_SHAREDLIB
      OFF
      CACHE BOOL "" FORCE)
  set(FLATBUFFERS_INSTALL
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(flatbuffers)

  target_link_libraries(libwal PUBLIC flatbuffers)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "========= wal configuration =========")
message(STATUS "  Protobuf      : ${UTILS_WAL_ENABLE_PROTOBUF}")
message(STATUS "  FlatBuffers   : ${UTILS_WAL_ENABLE_FLATBUFFERS}")
message(STATUS "========================================")
message(STATUS "")

