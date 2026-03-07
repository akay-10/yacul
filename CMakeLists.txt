cmake_minimum_required(VERSION 3.15)

project(
  yacul
  VERSION 0.0.1
  LANGUAGES CXX)

# Options for FetchContent dependencies - can be overridden by parent projects
option(UTILS_FETCH_GOOGLETEST "Fetch GoogleTest if not already available" ON)
option(UTILS_FETCH_ABSL "Fetch Abseil if not already available" ON)

# Options to use external packages instead of fetching
option(UTILS_USE_EXTERNAL_GOOGLETEST "Use externally provided GoogleTest" OFF)
option(UTILS_USE_EXTERNAL_ABSL "Use externally provided Abseil" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(UTILS_BUILD_TEST "Build the test suite" ON)

include(FetchContent)

# GoogleTest handling - only if tests are enabled
if(UTILS_BUILD_TEST)
  if(UTILS_USE_EXTERNAL_GOOGLETEST)
    find_package(GTest REQUIRED)
  elseif(UTILS_FETCH_GOOGLETEST)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.17.0)

    # Set GoogleTest options Controls whether GoogleMock (gmock) is built
    # alongside GoogleTest (gtest)
    set(BUILD_GMOCK ON)
    # Controls whether GoogleTest installs its headers and libraries to the
    # system
    set(INSTALL_GTEST OFF)

    FetchContent_MakeAvailable(googletest)
  else()
    message(
      FATAL_ERROR
        "GoogleTest is required for tests. "
        "Either set UTILS_FETCH_GOOGLETEST=ON to fetch GoogleTest, "
        "or set UTILS_USE_EXTERNAL_GOOGLETEST=ON and provide it via find_package(GTest)"
    )
  endif()
endif()

# Abseil handling - always required
if(UTILS_USE_EXTERNAL_ABSL)
  find_package(absl REQUIRED)
elseif(UTILS_FETCH_ABSL)
  FetchContent_Declare(
    absl
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20260107.0)

  # Set abseil-cpp options Make abseil use the same C++ standard as this project
  set(ABSL_PROPAGATE_CXX_STD ON)
  # Compiler warnings from Abseil headers are suppressed
  set(ABSL_USE_SYSTEM_INCLUDES OFF)
  # Controls whether Abseil's own test suite is built
  set(ABSL_BUILD_TESTING OFF)
  # Uses find_package(GTest) to locate GoogleTest
  set(ABSL_FIND_GOOGLETEST OFF)

  if(UTILS_BUILD_TEST)
    # Makes Abseil's test helpers available for your tests
    set(ABSL_BUILD_TEST_HELPERS ON)
    # Tells Abseil to use an externally-provided GoogleTest instead of fetching
    # its own
    set(ABSL_USE_EXTERNAL_GOOGLETEST ON)
  endif()

  FetchContent_MakeAvailable(absl)
else()
  message(
    FATAL_ERROR
      "Abseil is required. " "Either set UTILS_FETCH_ABSL=ON to fetch Abseil, "
      "or set UTILS_USE_EXTERNAL_ABSL=ON and provide it via find_package(absl)")
endif()

add_subdirectory(src)
