# ---------------------------------------------------------------------------
# Top-level feature flags Each flag controls both FetchContent download AND
# compilation of the corresponding adapter. Set to OFF to completely exclude a
# dependency.
# ---------------------------------------------------------------------------
option(UTILS_TRANSFORMATION_ENABLE_LZ4
       "Enable LZ4 compression adapter (fetched via FetchContent)" ON)

option(UTILS_TRANSFORMATION_ENABLE_ZSTD
       "Enable Zstd compression adapter (fetched via FetchContent)" ON)

option(UTILS_TRANSFORMATION_ENABLE_SNAPPY
       "Enable Snappy compression adapter (fetched via FetchContent)" ON)

option(UTILS_TRANSFORMATION_ENABLE_XXHASH
       "Enable xxHash checksum adapter (fetched via FetchContent)" ON)

option(UTILS_TRANSFORMATION_ENABLE_CRC32C
       "Enable CRC32C checksum adapter (fetched via FetchContent)" ON)

option(UTILS_TRANSFORMATION_ENABLE_BLAKE3
       "Enable BLAKE3 hash adapter (fetched via FetchContent)" ON)

option(UTILS_TRANSFORMATION_ENABLE_OPENSSL
       "Enable OpenSSL SHA hash adapter (system library)" ON)

# -- LZ4 --------------------------------------------------------------------
if(UTILS_TRANSFORMATION_ENABLE_LZ4)
  message(STATUS "Fetching LZ4 ...")
  FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG v1.10.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake)
  set(LZ4_BUILD_CLI
      OFF
      CACHE BOOL "" FORCE)
  set(LZ4_BUILD_LEGACY_LZ4C
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(lz4)
endif()

# -- Zstd -------------------------------------------------------------------
if(UTILS_TRANSFORMATION_ENABLE_ZSTD)
  message(STATUS "Fetching Zstd ...")
  FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.7
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake)
  set(ZSTD_BUILD_PROGRAMS
      OFF
      CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_CONTRIB
      OFF
      CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_SHARED
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(zstd)
endif()

# -- Snappy -----------------------------------------------------------------
if(UTILS_TRANSFORMATION_ENABLE_SNAPPY)
  message(STATUS "Fetching Snappy ...")
  FetchContent_Declare(
    snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG 1.2.2
    GIT_SHALLOW TRUE)
  set(SNAPPY_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE)
  set(SNAPPY_BUILD_BENCHMARKS
      OFF
      CACHE BOOL "" FORCE)
  set(SNAPPY_INSTALL
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(snappy)
endif()

# -- xxHash -----------------------------------------------------------------
if(UTILS_TRANSFORMATION_ENABLE_XXHASH)
  message(STATUS "Fetching xxHash ...")
  FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.3
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake_unofficial)
  set(XXHASH_BUILD_XXHSUM
      OFF
      CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(xxhash)
endif()

# -- CRC32C -----------------------------------------------------------------
if(UTILS_TRANSFORMATION_ENABLE_CRC32C)
  message(STATUS "Fetching CRC32C ...")
  FetchContent_Declare(
    crc32c
    GIT_REPOSITORY https://github.com/google/crc32c.git
    GIT_TAG 1.1.2
    GIT_SHALLOW TRUE)
  set(CRC32C_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE)
  set(CRC32C_BUILD_BENCHMARKS
      OFF
      CACHE BOOL "" FORCE)
  set(CRC32C_USE_GLOG
      OFF
      CACHE BOOL "" FORCE)
  set(CRC32C_INSTALL
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(crc32c)
endif()

# -- BLAKE3 -----------------------------------------------------------------
if(UTILS_TRANSFORMATION_ENABLE_BLAKE3)
  message(STATUS "Fetching BLAKE3 ...")
  FetchContent_Declare(
    blake3
    GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
    GIT_TAG 1.8.3
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR c)
  FetchContent_MakeAvailable(blake3)
endif()

# -- OpenSSL ----------------------------------------------------------------
if(UTILS_TRANSFORMATION_ENABLE_OPENSSL)
  find_package(OpenSSL REQUIRED)
  message(STATUS "Found OpenSSL ${OPENSSL_VERSION}")
endif()

# ---------------------------------------------------------------------------
# libtransformation library target
# ---------------------------------------------------------------------------
set(_DT_SOURCES transformer_factory.cc)
set(_DT_PUBLIC_HEADERS i_data_transformer.h transformer_factory.h hex_utils.h)

if(UTILS_TRANSFORMATION_ENABLE_LZ4)
  list(APPEND _DT_SOURCES lz4_compressor.cc)
  list(APPEND _DT_PUBLIC_HEADERS lz4_compressor.h)
endif()

if(UTILS_TRANSFORMATION_ENABLE_ZSTD)
  list(APPEND _DT_SOURCES zstd_compressor.cc)
  list(APPEND _DT_PUBLIC_HEADERS zstd_compressor.h)
endif()

if(UTILS_TRANSFORMATION_ENABLE_SNAPPY)
  list(APPEND _DT_SOURCES snappy_compressor.cc)
  list(APPEND _DT_PUBLIC_HEADERS snappy_compressor.h)
endif()

if(UTILS_TRANSFORMATION_ENABLE_XXHASH)
  list(APPEND _DT_SOURCES xxhash_checksummer.cc)
  list(APPEND _DT_PUBLIC_HEADERS xxhash_checksummer.h)
endif()

if(UTILS_TRANSFORMATION_ENABLE_CRC32C)
  list(APPEND _DT_SOURCES crc32c_checksummer.cc)
  list(APPEND _DT_PUBLIC_HEADERS crc32c_checksummer.h)
endif()

if(UTILS_TRANSFORMATION_ENABLE_BLAKE3)
  list(APPEND _DT_SOURCES blake3_hasher.cc)
  list(APPEND _DT_PUBLIC_HEADERS blake3_hasher.h)
endif()

if(UTILS_TRANSFORMATION_ENABLE_OPENSSL)
  list(APPEND _DT_SOURCES openssl_sha_hasher.cc)
  list(APPEND _DT_PUBLIC_HEADERS openssl_sha_hasher.h)
endif()

# Select static vs shared
if(UTILS_BUILD_SHARED)
  add_library(libtransformation SHARED ${_DT_SOURCES})
else()
  add_library(libtransformation STATIC ${_DT_SOURCES})
endif()

target_include_directories(
  libtransformation PUBLIC $<BUILD_INTERFACE:${YACUL_INCLUDE_ROOT}>
                           $<INSTALL_INTERFACE:include>)

# Compile-time feature macros — propagated to consumers via PUBLIC
if(UTILS_TRANSFORMATION_ENABLE_LZ4)
  target_compile_definitions(libtransformation
                             PUBLIC UTILS_TRANSFORMATION_HAS_LZ4)
  target_link_libraries(libtransformation PUBLIC lz4_static)
endif()

if(UTILS_TRANSFORMATION_ENABLE_ZSTD)
  target_compile_definitions(libtransformation
                             PUBLIC UTILS_TRANSFORMATION_HAS_ZSTD)
  target_link_libraries(libtransformation PUBLIC libzstd_static)
endif()

if(UTILS_TRANSFORMATION_ENABLE_SNAPPY)
  target_compile_definitions(libtransformation
                             PUBLIC UTILS_TRANSFORMATION_HAS_SNAPPY)
  target_link_libraries(libtransformation PUBLIC snappy)
endif()

if(UTILS_TRANSFORMATION_ENABLE_XXHASH)
  target_include_directories(
    libtransformation PUBLIC ${xxhash_SOURCE_DIR} # repo root — where xxhash.h
                                                  # actually is
  )
  target_compile_definitions(libtransformation
                             PUBLIC UTILS_TRANSFORMATION_HAS_XXHASH)
  target_link_libraries(libtransformation PUBLIC xxhash)
endif()

if(UTILS_TRANSFORMATION_ENABLE_CRC32C)
  target_compile_definitions(libtransformation
                             PUBLIC UTILS_TRANSFORMATION_HAS_CRC32C)
  target_link_libraries(libtransformation PUBLIC crc32c)
endif()

if(UTILS_TRANSFORMATION_ENABLE_BLAKE3)
  target_compile_definitions(libtransformation
                             PUBLIC UTILS_TRANSFORMATION_HAS_BLAKE3)
  target_link_libraries(libtransformation PUBLIC blake3)
endif()

if(UTILS_TRANSFORMATION_ENABLE_OPENSSL)
  target_compile_definitions(libtransformation
                             PUBLIC UTILS_TRANSFORMATION_HAS_OPENSSL)
  target_link_libraries(libtransformation PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

add_library(${PROJECT_NAME}::transformation ALIAS libtransformation)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(UTILS_BUILD_TEST)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "========= libtransformation v${PROJECT_VERSION} =========")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared lib   : ${UTILS_BUILD_SHARED}")
message(STATUS "  Tests        : ${UTILS_BUILD_TEST}")
message(STATUS "  -- Compressors --")
message(STATUS "  LZ4          : ${UTILS_TRANSFORMATION_ENABLE_LZ4}")
message(STATUS "  Zstd         : ${UTILS_TRANSFORMATION_ENABLE_ZSTD}")
message(STATUS "  Snappy       : ${UTILS_TRANSFORMATION_ENABLE_SNAPPY}")
message(STATUS "  -- Checksums --")
message(STATUS "  xxHash       : ${UTILS_TRANSFORMATION_ENABLE_XXHASH}")
message(STATUS "  CRC32C       : ${UTILS_TRANSFORMATION_ENABLE_CRC32C}")
message(STATUS "  -- Hashing --")
message(STATUS "  BLAKE3       : ${UTILS_TRANSFORMATION_ENABLE_BLAKE3}")
message(STATUS "  OpenSSL SHA  : ${UTILS_TRANSFORMATION_ENABLE_OPENSSL}")
message(STATUS "=============================================")
message(STATUS "")
