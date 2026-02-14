#!/bin/bash
set -e

BUILD_DIR="build"
BASE_DIR="src"
TEST_DIR_NAME="tests"
VERBOSE=false
CONTINUE_ON_FAILURE=false
FILTER=""
TIMEOUT=0

# Color output
if [[ -t 1 ]]; then
  RED='\033[0;31m'
  GREEN='\033[0;32m'
  YELLOW='\033[1;33m'
  BLUE='\033[0;34m'
  CYAN='\033[0;36m'
  MAGENTA='\033[0;35m'
  NC='\033[0m'
else
  RED=''
  GREEN=''
  YELLOW=''
  BLUE=''
  CYAN=''
  MAGENTA=''
  NC=''
fi

# Logging functions
log_info() {
  echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
  echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
  echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
  echo -e "${RED}[ERROR]${NC} $1"
}

log_debug() {
  if [ "$VERBOSE" = true ]; then
    echo -e "${MAGENTA}[DEBUG]${NC} $1" >&2
  fi
}

log_test_header() {
  echo -e "${CYAN}=========================================${NC}"
  echo -e "${CYAN}Running test: ${MAGENTA}$1${NC}"
  echo -e "${CYAN}=========================================${NC}"
}

log_separator() {
  echo -e "${CYAN}=========================================${NC}"
}

# Print usage
print_usage() {
  cat <<EOF
Usage: $0 [OPTIONS] [SUBDIR] [TEST_NAME]

Description:
  Run test executables from the build directory.
  If no arguments are provided, runs all tests.
  If SUBDIR and TEST_NAME are provided, runs that specific test.

Options:
  -v, --verbose            Show detailed test output
  -c, --continue           Continue running tests even if one fails
  -f, --filter PATTERN     Only run tests matching pattern (grep regex)
  -t, --timeout SECONDS    Set timeout for each test (0 = no timeout)
  -l, --list               List all available tests without running
  -d, --debug              Show debug information about test discovery
  -h, --help               Show this help message

Arguments:
  SUBDIR                   Test subdirectory name (e.g., 'myproject')
  TEST_NAME                Specific test executable name

Examples:
  $0                           # Run all tests
  $0 -v                        # Run all tests with verbose output
  $0 --filter "unit"           # Run only tests matching "unit"
  $0 myproject test_basics     # Run specific test
  $0 -l                        # List all available tests
  $0 -d                        # Show debug info about test discovery
  $0 -c -t 30                  # Run all tests with 30s timeout, continue on failure

Environment Variables:
  TEST_BUILD_DIR               Override build directory (default: build)
  TEST_BASE_DIR                Override base source directory (default: src)
EOF
}

# Override defaults from environment
[ -n "$TEST_BUILD_DIR" ] && BUILD_DIR="$TEST_BUILD_DIR"
[ -n "$TEST_BASE_DIR" ] && BASE_DIR="$TEST_BASE_DIR"

# Parse command line arguments
parse_args() {
  local positional_args=()
  local list_only=false
  local debug_mode=false

  while [[ $# -gt 0 ]]; do
    case $1 in
    -v | --verbose)
      VERBOSE=true
      shift
      ;;
    -c | --continue)
      CONTINUE_ON_FAILURE=true
      shift
      ;;
    -f | --filter)
      FILTER="$2"
      shift 2
      ;;
    -t | --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    -l | --list)
      list_only=true
      shift
      ;;
    -d | --debug)
      debug_mode=true
      shift
      ;;
    -h | --help)
      print_usage
      exit 0
      ;;
    -*)
      log_error "Unknown option: $1"
      print_usage
      exit 1
      ;;
    *)
      positional_args+=("$1")
      shift
      ;;
    esac
  done

  if [ "$debug_mode" = true ]; then
    VERBOSE=true
    debug_test_discovery
    exit 0
  fi

  if [ "$list_only" = true ]; then
    list_tests
    exit 0
  fi

  # Restore positional arguments
  set -- "${positional_args[@]}"

  # Export for subprocesses
  export TEST_SUBDIR_NAME="$1"
  export TEST_NAME="$2"
}

# Debug test discovery
debug_test_discovery() {
  log_info "Debug: Test Discovery Information"
  log_separator
  echo "Build directory: $BUILD_DIR"
  echo "Base directory: $BASE_DIR"
  echo "Test directory name: $TEST_DIR_NAME"
  echo ""

  log_info "Searching in: $BUILD_DIR/$BASE_DIR/"

  if [ ! -d "$BUILD_DIR/$BASE_DIR" ]; then
    log_error "Base directory does not exist: $BUILD_DIR/$BASE_DIR"
    return
  fi

  log_info "Subdirectories found:"
  for sub_dir in "$BUILD_DIR/$BASE_DIR/"*; do
    if [ -d "$sub_dir" ]; then
      local subdir_name=$(basename "$sub_dir")
      echo "  - $subdir_name"

      local test_dir="$sub_dir/$TEST_DIR_NAME"
      if [ -d "$test_dir" ]; then
        echo "    └─ Test directory exists: $test_dir"

        # List all files in test directory
        echo "       Files in test directory:"
        for file in "$test_dir"/*; do
          if [ -e "$file" ]; then
            local filename=$(basename "$file")
            if [ -d "$file" ]; then
              echo "       ├─ [DIR] $filename"
            elif [ -x "$file" ]; then
              echo "       ├─ [EXECUTABLE] $filename"
            elif [ -f "$file" ]; then
              echo "       ├─ [FILE] $filename"
            fi
          fi
        done
      else
        echo "    └─ No test directory found at: $test_dir"
      fi
      echo ""
    fi
  done

  log_info "Tests identified by is_test_executable():"
  for sub_dir in "$BUILD_DIR/$BASE_DIR/"*; do
    if [ ! -d "$sub_dir" ]; then
      continue
    fi

    local test_dir="$sub_dir/$TEST_DIR_NAME"
    if [ -d "$test_dir" ]; then
      while IFS= read -r -d '' file; do
        if is_test_executable "$file"; then
          echo "  ✓ $(basename "$(dirname "$(dirname "$file")")")/$(basename "$file")"
        else
          echo "  ✗ $(basename "$(dirname "$(dirname "$file")")")/$(basename "$file") (rejected)"
        fi
      done < <(find "$test_dir" -maxdepth 1 -type f -print0 2>/dev/null | sort -z)
    fi
  done

  log_separator
}

# Check if build directory exists
check_build_dir() {
  if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    log_warning "Build directory not found or not configured."

    if [ -f "./build.sh" ]; then
      log_info "Building project first..."
      ./build.sh build
    else
      log_error "Please run './build.sh build' first"
      exit 1
    fi
  fi
}

# Check if a file is a test executable
is_test_executable() {
  local file="$1"

  # Must be a regular file (not directory)
  if [ ! -f "$file" ]; then
    return 1
  fi

  # Must be executable
  if [ ! -x "$file" ]; then
    return 1
  fi

  # Get basename for pattern matching
  local basename=$(basename "$file")

  # Exclude CMake-related patterns
  case "$basename" in
  CMake* | *.cmake)
    return 1
    ;;
  *.sh | *.py | *.pl | *.rb)
    return 1
    ;;
  *.txt | *.md | *.html | *.xml | *.json)
    return 1
    ;;
  esac

  # If file command is available, use it for better detection
  if command -v file &>/dev/null; then
    local filetype=$(file -b "$file" 2>/dev/null)

    # Check if it's actually an executable binary
    if echo "$filetype" | grep -qE "(ELF.*executable|Mach-O.*executable|PE32.*executable)"; then
      log_debug "Accepted $file: executable binary"
      return 0
    fi

    # Reject if it's clearly not an executable
    if echo "$filetype" | grep -qE "(directory|symbolic link|text|script|ASCII|UTF-8)"; then
      return 1
    fi
  fi

  # Fallback: if it's executable and passed all checks, accept it
  return 0
}

# Find all test executables
find_all_tests() {
  local tests=()

  log_debug "Searching for tests in: $BUILD_DIR/$BASE_DIR/*/tests/"

  for sub_dir in "$BUILD_DIR/$BASE_DIR/"*; do
    if [ ! -d "$sub_dir" ]; then
      continue
    fi

    local subdir_name=$(basename "$sub_dir")
    local test_dir="$sub_dir/$TEST_DIR_NAME"

    log_debug "Checking test directory: $test_dir"

    if [ -d "$test_dir" ]; then
      # Only look at files directly in the test directory (maxdepth 1)
      # Exclude CMakeFiles and other build artifacts
      while IFS= read -r -d '' file; do
        if is_test_executable "$file"; then
          tests+=("$file")
          log_debug "Added test: $file"
        fi
      done < <(find "$test_dir" -maxdepth 1 -type f -executable -print0 2>/dev/null | sort -z)
    fi
  done

  printf '%s\n' "${tests[@]}"
}

# List all available tests
list_tests() {
  # Temporarily disable exit on error for this function
  set +e

  check_build_dir

  log_info "Available tests:"
  echo ""

  local count=0

  for sub_dir in "$BUILD_DIR/$BASE_DIR/"*; do
    [ ! -d "$sub_dir" ] && continue

    local subdir_name=$(basename "$sub_dir")
    local test_dir="$sub_dir/$TEST_DIR_NAME"

    [ ! -d "$test_dir" ] && continue

    local subdir_tests=()

    # Use maxdepth 1 to only look in the test directory itself, not subdirectories
    while IFS= read -r -d '' file; do
      if is_test_executable "$file"; then
        subdir_tests+=("$(basename "$file")")
      fi
    done < <(find "$test_dir" -maxdepth 1 -type f -executable -print0 2>/dev/null | sort -z)

    if [ ${#subdir_tests[@]} -gt 0 ]; then
      echo -e "${CYAN}$subdir_name:${NC}"
      for test_name in "${subdir_tests[@]}"; do
        echo "  - $test_name"
        count=$((count + 1))
      done
      echo ""
    fi
  done

  if [ $count -eq 0 ]; then
    log_warning "No test executables found"
    echo ""
    log_info "Searched in: $BUILD_DIR/$BASE_DIR/*/tests/"
    log_info "Run with --debug flag for more information:"
    echo "  $0 --debug"
  else
    log_success "Total: $count tests"
  fi

  # Re-enable exit on error
  set -e
}

# Run a single test with optional timeout
run_single_test() {
  local test_exec="$1"
  local test_name=$(basename "$test_exec")
  local subdir=$(basename "$(dirname "$(dirname "$test_exec")")")

  # Apply filter if specified
  if [ -n "$FILTER" ]; then
    if ! echo "$test_name" | grep -qE "$FILTER"; then
      log_debug "Skipping $test_name (doesn't match filter)"
      return 2 # Return 2 to indicate skipped
    fi
  fi

  log_test_header "$subdir/$test_name"

  local start_time=$(date +%s)
  local test_result=0

  # Temporarily disable exit on error for test execution
  set +e

  if [ $TIMEOUT -gt 0 ]; then
    if command -v timeout &>/dev/null; then
      if [ "$VERBOSE" = true ]; then
        timeout $TIMEOUT "$test_exec"
        test_result=$?
      else
        timeout $TIMEOUT "$test_exec" >/dev/null 2>&1
        test_result=$?
      fi

      if [ $test_result -eq 124 ]; then
        log_error "Test timed out after ${TIMEOUT}s"
        set -e
        return 1
      fi
    else
      log_warning "timeout command not available, running without timeout"
      if [ "$VERBOSE" = true ]; then
        "$test_exec"
        test_result=$?
      else
        "$test_exec" >/dev/null 2>&1
        test_result=$?
      fi
    fi
  else
    if [ "$VERBOSE" = true ]; then
      "$test_exec"
      test_result=$?
    else
      "$test_exec" >/dev/null 2>&1
      test_result=$?
    fi
  fi

  # Re-enable exit on error
  set -e

  local end_time=$(date +%s)
  local duration=$((end_time - start_time))

  if [ $test_result -eq 0 ]; then
    log_success "Test passed (${duration}s)"
    return 0
  else
    log_error "Test failed with exit code $test_result (${duration}s)"
    return 1
  fi
}

# Run all tests
run_all_tests() {
  # Temporarily disable exit on error to allow test failures
  set +e

  log_info "Discovering tests..."

  local tests=()
  while IFS= read -r test_exec; do
    [ -n "$test_exec" ] && tests+=("$test_exec")
  done < <(find_all_tests)

  # Re-enable for the test discovery part
  set -e

  if [ ${#tests[@]} -eq 0 ]; then
    log_warning "No test executables found in $BUILD_DIR/$BASE_DIR/*/tests/"
    log_info "Run with --debug flag for more information:"
    echo "  $0 --debug"
    exit 0
  fi

  local total=${#tests[@]}
  local passed=0
  local failed=0
  local skipped=0

  log_info "Found $total test(s)"
  [ -n "$FILTER" ] && log_info "Filter: '$FILTER'"
  [ $TIMEOUT -gt 0 ] && log_info "Timeout: ${TIMEOUT}s per test"
  echo ""

  local overall_start=$(date +%s)

  # Disable exit on error for test execution
  set +e

  for test_exec in "${tests[@]}"; do
    local result
    run_single_test "$test_exec"
    result=$?

    case $result in
    0)
      passed=$((passed + 1))
      ;;
    1)
      failed=$((failed + 1))
      if [ "$CONTINUE_ON_FAILURE" = false ]; then
        log_error "Stopping test run due to failure"
        break
      fi
      ;;
    2)
      skipped=$((skipped + 1))
      ;;
    esac
    echo ""
  done

  # Re-enable exit on error
  set -e

  local overall_end=$(date +%s)
  local overall_duration=$((overall_end - overall_start))

  # Summary
  log_separator
  log_info "Test Summary"
  log_separator
  echo -e "  Total:    $total"
  echo -e "  ${GREEN}Passed:   $passed${NC}"
  [ $failed -gt 0 ] && echo -e "  ${RED}Failed:   $failed${NC}" || echo -e "  Failed:   $failed"
  [ $skipped -gt 0 ] && echo -e "  ${YELLOW}Skipped:  $skipped${NC}"
  echo -e "  Duration: ${overall_duration}s"
  log_separator

  if [ $failed -gt 0 ]; then
    log_error "Some tests failed!"
    exit 1
  else
    log_success "All tests passed!"
  fi
}

# Run specific test
run_specific_test() {
  local test_subdir="$BUILD_DIR/$BASE_DIR/$TEST_SUBDIR_NAME/$TEST_DIR_NAME"
  local test_exec="$test_subdir/$TEST_NAME"

  if [ -f "$test_exec" ] && [ -x "$test_exec" ]; then
    run_single_test "$test_exec"
    exit $?
  else
    log_error "Test executable '$test_exec' not found or not executable"
    echo ""
    log_info "Available tests in '$test_subdir':"
    if [ -d "$test_subdir" ]; then
      local found_tests=false
      while IFS= read -r -d '' file; do
        if is_test_executable "$file"; then
          echo "  - $(basename "$file")"
          found_tests=true
        fi
      done < <(find "$test_subdir" -maxdepth 1 -type f -executable -print0 2>/dev/null | sort -z)

      if [ "$found_tests" = false ]; then
        echo "  (no executable tests found)"
      fi
    else
      echo "  (test directory not found)"
    fi
    exit 1
  fi
}

# Main execution
main() {
  parse_args "$@"
  check_build_dir

  if [ -n "$TEST_SUBDIR_NAME" ] && [ -n "$TEST_NAME" ]; then
    # Run specific test
    run_specific_test
  else
    # Run all tests
    run_all_tests
  fi
}

main "$@"
