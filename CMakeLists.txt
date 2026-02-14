cmake_minimum_required(VERSION 3.15)

project(Utils VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TEST "Build the test suite" ON)

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

if(BUILD_TEST)
  # Set GoogleTest options
  # Controls whether GoogleMock (gmock) is built alongside GoogleTest (gtest)
  set(BUILD_GMOCK ON)
  # Controls whether GoogleTest installs its headers and libraries to the system
  set(INSTALL_GTEST OFF)
endif()

if(BUILD_TEST)
  FetchContent_MakeAvailable(googletest)
endif()

FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG 20260107.0
)

# Set abseil-cpp options
# Make abseil use the same C++ standard as this project
set(ABSL_PROPAGATE_CXX_STD ON)
# Compiler warnings from Abseil headers are suppressed
set(ABSL_USE_SYSTEM_INCLUDES OFF)
# Controls whether Abseil's own test suite is built
set(ABSL_BUILD_TESTING OFF)
# Uses find_package(GTest) to locate GoogleTest
set(ABSL_FIND_GOOGLETEST OFF)

if(BUILD_TEST)
  # Makes Abseil's test helpers available for your tests
  set(ABSL_BUILD_TEST_HELPERS ON)
  # Tells Abseil to use an externally-provided GoogleTest instead of fetching
  # its own
  set(ABSL_USE_EXTERNAL_GOOGLETEST ON)
endif()

FetchContent_MakeAvailable(absl)

add_subdirectory(src)

