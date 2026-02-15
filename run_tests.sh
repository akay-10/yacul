#!/bin/bash
set -e

BUILD_DIR="build"
BASE_DIR="src"
TEST_DIR_NAME="tests"
VERBOSE=false
CONTINUE_ON_FAILURE=false
FILTER=""

# Color output. Init colors only when this process's fd 1 (stdout) is connected
# to a terminal.
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

log_separator() {
  echo -e "${CYAN}=========================================${NC}"
}

log_test_header() {
  log_separator
  echo -e "${CYAN}Running test: ${MAGENTA}$1${NC}"
  log_separator
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
  -l, --list               List all available tests without running
  -h, --help               Show this help message

Arguments:
  SUBDIR                   Test subdirectory name (e.g., 'basic or hashing')
  TEST_NAME                Specific test executable name

Examples:
  $0                       # Run all tests
  $0 -v                    # Run all tests with verbose output
  $0 --filter "unit"       # Run only tests matching "unit"
  $0 basic basic_test      # Run specific test
  $0 -l                    # List all available tests
  $0 -c                    # Run all tests and continue on failure

Environment Variables:
  TEST_BUILD_DIR           Override build directory (default: build)
  TEST_BASE_DIR            Override base source directory (default: src)
EOF
}

# Override defaults from environment
[ -n "$TEST_BUILD_DIR" ] && BUILD_DIR="$TEST_BUILD_DIR"
[ -n "$TEST_BASE_DIR" ] && BASE_DIR="$TEST_BASE_DIR"

main() {
  # Parse command line arguments
  local positional_args=()
  local list_only=false

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
    -l | --list)
      list_only=true
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

  check_build_dir

  if [ "$list_only" = true ]; then
    discover_tests false
    exit $?
  fi

  # Parse subdir and test name to run specific tests from positional arguments.
  set -- "${positional_args[@]}"
  TEST_SUBDIR_NAME="$1"
  TEST_NAME="$2"

  if [ -n "$TEST_SUBDIR_NAME" ] && [ -n "$TEST_NAME" ]; then
    # Run specific test
    run_specific_test "$TEST_SUBDIR_NAME" "$TEST_NAME"
    exit $?
  else
    # Run all tests
    run_all_tests
    exit $?
  fi
}

# Check if build directory exists
check_build_dir() {
  if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    log_warning "Build directory not found or not configured."
    if [ -f "./build.sh" ]; then
      log_info "Please build the project first using './build.sh build'"
    else
      log_error "./build.sh not found. Please build the project first manually"
      log_error "Default build dir: $BUILD_DIR, available env var: TEST_BUILD_DIR to override default"
      exit 1
    fi
  fi
}

# Discover tests
discover_tests() {
  local dump_tests_lists=$1

  log_info "Searching for tests in: $BUILD_DIR/$BASE_DIR/" >&2
  log_separator >&2
  log_debug "Build directory: $BUILD_DIR"
  log_debug "Base directory: $BASE_DIR"
  log_debug "Test directory name: $TEST_DIR_NAME"

  if [ ! -d "$BUILD_DIR/$BASE_DIR" ]; then
    log_error "Base directory does not exist: $BUILD_DIR/$BASE_DIR" >&2
    return 1
  fi

  local tests=()

  for sub_dir in "$BUILD_DIR/$BASE_DIR/"*; do
    while IFS= read -r test_exec; do
      [ -n "$test_exec" ] && tests+=("$test_exec")
    done < <(discover_tests_subdir "$sub_dir")
  done

  log_separator >&2
  if [ "$dump_tests_lists" = true ]; then
    printf '%s\n' "${tests[@]}"
  fi
}

# Discover tests in a subdir
discover_tests_subdir() {
  local sub_dir="$1"
  local subdir_name=""
  subdir_name=$(basename "$sub_dir")

  local tests=()

  if [ -d "$sub_dir" ]; then
    log_debug "Discovering tests in $sub_dir"
    log_debug "  - ${YELLOW}[DIR]${NC} $subdir_name"
    local test_dir="$sub_dir/$TEST_DIR_NAME"

    if [ -d "$test_dir" ]; then
      log_debug "    └─ Test directory exists: $test_dir"

      for file in "$test_dir"/*; do
        if [ -e "$file" ]; then
          local filename=""
          filename=$(basename "$file")

          if [ -d "$file" ]; then
            log_debug "       ├─ ${YELLOW}[DIR]${NC} $filename"
          elif [ -x "$file" ]; then
            log_debug "       ├─ ${GREEN}[EXE]${NC} $filename"
          elif [ -f "$file" ]; then
            log_debug "       ├─ ${BLUE}[FILE]${NC} $filename"
          fi
        fi
      done

      # Again search using find
      while IFS= read -r -d '' file; do
        if is_test_executable "$file"; then
          tests+=("$file")
          log_debug "Added test: $file"
        fi
      done < <(find "$test_dir" -maxdepth 1 -type f -executable -print0 2>/dev/null | sort -z)

    else
      log_debug "    └─ No test directory found at: $test_dir"
    fi
  elif [ -f "$sub_dir" ]; then
    log_debug "  - ${BLUE}[FILE/EXE]${NC} $subdir_name"
  else
    log_debug "  - ${RED}[UNKNOWN]${NC} $subdir_name"
  fi

  if [ ${#tests[@]} -gt 0 ]; then
    log_info "${CYAN}$subdir_name${NC}:" >&2
    for test in "${tests[@]}"; do
      log_info "  - ${GREEN}$(basename "$test")${NC}" >&2
    done
  fi

  printf '%s\n' "${tests[@]}"
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
  local basename=""
  basename=$(basename "$file")

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
    local filetype=""
    filetype=$(file -b "$file" 2>/dev/null)

    # Check if it's actually an executable binary
    if echo "$filetype" | grep -qE "(ELF.*executable|Mach-O.*executable|PE32.*executable)"; then
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

# Run all tests
run_all_tests() {
  local tests=()
  while IFS= read -r test_exec; do
    [ -n "$test_exec" ] && tests+=("$test_exec")
  done < <(discover_tests true)

  if [ "${#tests[@]}" -eq 0 ]; then
    log_warning "No test executables found in $BUILD_DIR/$BASE_DIR/*/tests/"
    log_info "Run with -v flag for more information: $0 -v"
    return 0
  fi

  local total="${#tests[@]}"
  local passed=0
  local failed=0
  local skipped=0

  log_info "Found $total test(s)"
  [ -n "$FILTER" ] && log_info "Filter: '$FILTER'"

  local overall_start=0
  local overall_end=0

  overall_start="$(date +%s)"

  for test_exec in "${tests[@]}"; do
    local result
    run_single_test "$test_exec"
    result=$?

    case "$result" in
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
  done

  overall_end="$(date +%s)"
  local overall_duration="$((overall_end - overall_start))"

  # Summary
  log_separator
  log_info "Test Summary"
  log_separator
  echo -e "  Total:    $total"
  echo -e "  ${GREEN}Passed:   $passed${NC}"
  [ "$failed" -gt 0 ] && echo -e "  ${RED}Failed:   $failed${NC}"
  [ "$skipped" -gt 0 ] && echo -e "  ${YELLOW}Skipped:  $skipped${NC}"
  echo -e "  Duration: ${overall_duration}s"
  log_separator

  if [ "$failed" -gt 0 ]; then
    log_error "Some tests failed!"
    return 1
  else
    log_success "All tests passed!"
  fi

  return 0
}

# Run specific test
run_specific_test() {
  local test_subdir="$BUILD_DIR/$BASE_DIR/$1/$TEST_DIR_NAME"
  local test_exec="$test_subdir/$2"

  if [ -f "$test_exec" ] && [ -x "$test_exec" ]; then
    run_single_test "$test_exec"
    return $?
  else
    log_error "Test executable '$test_exec' not found or not executable"
    log_info "Available tests in $(dirname "$test_subdir"):"
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
    return 1
  fi
}

# Run a single test
run_single_test() {
  local test_exec="$1"
  local test_name=""
  local subdir=""

  test_name=$(basename "$test_exec")
  subdir=$(basename "$(dirname "$(dirname "$test_exec")")")

  # Apply filter if specified
  if [ -n "$FILTER" ]; then
    if ! echo "$test_name" | grep -qE "$FILTER"; then
      log_debug "Skipping $test_name (doesn't match filter)"
      return 2 # Return 2 to indicate skipped
    fi
  fi

  log_test_header "$subdir $test_name"

  local start_time=0
  local end_time=0
  local test_result=0

  start_time=$(date +%s)

  if [ "$VERBOSE" = true ]; then
    "$test_exec"
    test_result=$?
  else
    "$test_exec" >/dev/null 2>&1
    test_result=$?
  fi

  end_time=$(date +%s)
  local duration=$((end_time - start_time))

  if [ $test_result -eq 0 ]; then
    log_success "Test passed (${duration}s)"
    return 0
  else
    log_error "Test failed with exit code $test_result (${duration}s)"
    return 1
  fi
}

# Main execution
main "$@"
