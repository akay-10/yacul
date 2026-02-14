#!/bin/bash

set -e

BUILD_DIR="build"
BASE_DIR="src"
TEST_DIR_NAME="tests"

# Check if build directory exists and has been configured
if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
	echo "Build directory not found or not configured. Building project first..."
	./build.sh build
fi

# Parse command line arguments
if [ $# -eq 0 ]; then
	# Run all tests if no argument provided
	echo "Running all tests..."

  for sub_dir in "$BUILD_DIR/$BASE_DIR/"*; do
    TEST_DIR="$sub_dir/$TEST_DIR_NAME"
    if [ -d "$TEST_DIR" ]; then
      for test_exec in "$TEST_DIR"/*; do
        if [ -f "$test_exec" ] && [ -x "$test_exec" ]; then
          echo "========================================="
          echo "Running test: $(basename "$test_exec")"
          $test_exec
        fi
      done
    fi
  done
else
	# Run specific test
  TEST_SUBDIR_NAME="$1"
	TEST_NAME="$2"

  TEST_SUBDIR="$BUILD_DIR/$BASE_DIR/$TEST_SUBDIR_NAME/$TEST_DIR_NAME"
	TEST_EXEC="$TEST_SUBDIR/$TEST_NAME"
	
	if [ -f "$TEST_EXEC" ] && [ -x "$TEST_EXEC" ]; then
    echo "========================================="
    echo "Running test: $(basename "$TEST_NAME")"
		$TEST_EXEC
	else
		echo "Error: Test executable '$TEST_EXEC' not found."
		echo "Available tests in '$TEST_SUBDIR':"
		if [ -d "$TEST_SUBDIR" ]; then
			find "$TEST_SUBDIR" -type f -executable -printf "  %f\n" 2>/dev/null || ls -1 "$TEST_SUBDIR" 2>/dev/null || echo "  (none found)"
		else
			echo "  (test directory not found)"
		fi
		exit 1
	fi
fi

echo "========================================="
echo "All tests completed successfully!"

