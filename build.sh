#!/bin/bash
set -e

# Default configuration
BUILD_DIR="build"
CMAKE_CMD="cmake"
GENERATOR="Ninja"
BUILD_TYPE="Release"
VERBOSE=false
PARALLEL_JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
RUN_TESTS=false
INSTALL_PREFIX=""
SANITIZERS=""
CCACHE_ENABLED=false
ENABLE_TESTS=true

# Color output
if [[ -t 1 ]]; then
  RED='\033[0;31m'
  GREEN='\033[0;32m'
  YELLOW='\033[1;33m'
  BLUE='\033[0;34m'
  CYAN='\033[0;36m'
  MAGENTA='\033[0;35m'
  NC='\033[0m' # No Color
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

log_step() {
  echo -e "${CYAN}==>${NC} $1"
}

log_debug() {
  if [ "$VERBOSE" = true ]; then
    echo -e "${MAGENTA}[DEBUG]${NC} $1"
  fi
}

# Print usage information
print_usage() {
  cat <<EOF
Usage: $0 [COMMAND] [OPTIONS]

Commands:
  build      - Configure and build the project (default)
  configure  - Configure CMake only
  clean      - Remove build directory
  rebuild    - Clean and rebuild
  test       - Build and run tests (uses run_tests.sh)
  install    - Build and install the project
  info       - Show build configuration information

Options:
  -d, --debug              Build in Debug mode (default: Release)
  -r, --release            Build in Release mode
  --relwithdebinfo         Build in RelWithDebInfo mode
  --minsizerel             Build in MinSizeRel mode
  -v, --verbose            Enable verbose build output
  -j, --jobs N             Number of parallel jobs (default: $PARALLEL_JOBS)
  -t, --run-tests          Run tests after building
  --no-tests               Disable building tests
  --sanitize TYPE          Enable sanitizers (address, thread, undefined, memory)
  --ccache                 Enable ccache for faster rebuilds
  --install-prefix PATH    Installation prefix (default: /usr/local)
  --no-ninja               Don't use Ninja generator
  -h, --help               Show this help message

Examples:
  $0 build --debug --verbose
  $0 rebuild -d -j 8 --run-tests
  $0 test --sanitize address
  $0 install --install-prefix ~/.local
  $0 info

Environment Variables:
  CMAKE_ARGS               Additional CMake arguments
  CXX                      C++ compiler to use
  CC                       C compiler to use
EOF
}

# Check for required tools
check_dependencies() {
  local missing_deps=()

  if ! command -v cmake &>/dev/null; then
    missing_deps+=("cmake")
  fi

  if [ "$GENERATOR" = "Ninja" ] && ! command -v ninja &>/dev/null; then
    log_warning "ninja not found. Install it with:"
    echo "  Fedora/RHEL: sudo dnf install ninja-build"
    echo "  Ubuntu/Debian: sudo apt install ninja-build"
    echo "  macOS: brew install ninja"
    echo ""
    log_info "Falling back to default CMake generator..."
    GENERATOR=""
  fi

  if [ "$CCACHE_ENABLED" = true ] && ! command -v ccache &>/dev/null; then
    log_warning "ccache not found but --ccache was specified"
    CCACHE_ENABLED=false
  fi

  if [ ${#missing_deps[@]} -gt 0 ]; then
    log_error "Missing required dependencies: ${missing_deps[*]}"
    exit 1
  fi
}

# Parse command line arguments
parse_args() {
  COMMAND="${1:-build}"
  shift || true

  while [[ $# -gt 0 ]]; do
    case $1 in
    -d | --debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    -r | --release)
      BUILD_TYPE="Release"
      shift
      ;;
    --relwithdebinfo)
      BUILD_TYPE="RelWithDebInfo"
      shift
      ;;
    --minsizerel)
      BUILD_TYPE="MinSizeRel"
      shift
      ;;
    -v | --verbose)
      VERBOSE=true
      shift
      ;;
    -j | --jobs)
      PARALLEL_JOBS="$2"
      shift 2
      ;;
    -t | --run-tests)
      RUN_TESTS=true
      shift
      ;;
    --no-tests)
      ENABLE_TESTS=false
      shift
      ;;
    --sanitize)
      SANITIZERS="$2"
      shift 2
      ;;
    --ccache)
      CCACHE_ENABLED=true
      shift
      ;;
    --install-prefix)
      INSTALL_PREFIX="$2"
      shift 2
      ;;
    --no-ninja)
      GENERATOR=""
      shift
      ;;
    -h | --help)
      print_usage
      exit 0
      ;;
    *)
      log_error "Unknown option: $1"
      print_usage
      exit 1
      ;;
    esac
  done
}

# Build CMake configuration arguments
build_cmake_args() {
  local args="-S . -B $BUILD_DIR"

  if [ -n "$GENERATOR" ]; then
    args="$args -G $GENERATOR"
  fi

  args="$args -DCMAKE_BUILD_TYPE=$BUILD_TYPE"

  if [ -n "$INSTALL_PREFIX" ]; then
    args="$args -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
  fi

  if [ "$CCACHE_ENABLED" = true ]; then
    args="$args -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache"
  fi

  # Enable/disable tests
  if [ "$ENABLE_TESTS" = false ]; then
    args="$args -DBUILD_TESTING=OFF"
  else
    args="$args -DBUILD_TESTING=ON"
  fi

  # Add sanitizers
  if [ -n "$SANITIZERS" ]; then
    case "$SANITIZERS" in
    address | asan)
      args="$args -DCMAKE_CXX_FLAGS=\"-fsanitize=address -fno-omit-frame-pointer -g\""
      args="$args -DCMAKE_C_FLAGS=\"-fsanitize=address -fno-omit-frame-pointer -g\""
      log_debug "Enabled AddressSanitizer"
      ;;
    thread | tsan)
      args="$args -DCMAKE_CXX_FLAGS=\"-fsanitize=thread -g\""
      args="$args -DCMAKE_C_FLAGS=\"-fsanitize=thread -g\""
      log_debug "Enabled ThreadSanitizer"
      ;;
    undefined | ubsan)
      args="$args -DCMAKE_CXX_FLAGS=\"-fsanitize=undefined -g\""
      args="$args -DCMAKE_C_FLAGS=\"-fsanitize=undefined -g\""
      log_debug "Enabled UndefinedBehaviorSanitizer"
      ;;
    memory | msan)
      args="$args -DCMAKE_CXX_FLAGS=\"-fsanitize=memory -g\""
      args="$args -DCMAKE_C_FLAGS=\"-fsanitize=memory -g\""
      log_debug "Enabled MemorySanitizer"
      ;;
    *)
      log_error "Unknown sanitizer type: $SANITIZERS"
      log_info "Valid options: address, thread, undefined, memory"
      exit 1
      ;;
    esac
  fi

  # Add any additional CMAKE_ARGS from environment
  if [ -n "$CMAKE_ARGS" ]; then
    args="$args $CMAKE_ARGS"
    log_debug "Additional CMake args: $CMAKE_ARGS"
  fi

  # Export compile commands for tools like clangd
  args="$args -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

  echo "$args"
}

# Show build configuration
show_info() {
  log_step "Build Configuration"
  echo "  Build directory:    $BUILD_DIR"
  echo "  Build type:         $BUILD_TYPE"
  echo "  Generator:          ${GENERATOR:-default}"
  echo "  Parallel jobs:      $PARALLEL_JOBS"
  echo "  Verbose:            $VERBOSE"
  echo "  Tests enabled:      $ENABLE_TESTS"
  echo "  Run tests:          $RUN_TESTS"
  [ -n "$SANITIZERS" ] && echo "  Sanitizers:         $SANITIZERS"
  [ "$CCACHE_ENABLED" = true ] && echo "  CCache:             enabled"
  [ -n "$INSTALL_PREFIX" ] && echo "  Install prefix:     $INSTALL_PREFIX"
  [ -n "$CXX" ] && echo "  C++ compiler:       $CXX"
  [ -n "$CC" ] && echo "  C compiler:         $CC"

  if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo ""
    log_info "CMake cache exists at $BUILD_DIR/CMakeCache.txt"
    if [ "$VERBOSE" = true ]; then
      echo ""
      log_step "Key CMake Variables"
      grep -E "CMAKE_BUILD_TYPE|CMAKE_CXX_COMPILER|CMAKE_C_COMPILER|CMAKE_INSTALL_PREFIX" "$BUILD_DIR/CMakeCache.txt" | grep -v "^//" || true
    fi
  fi
}

# Configure the project
configure_project() {
  log_step "Configuring project"
  log_info "Build type: $BUILD_TYPE"
  log_info "Generator: ${GENERATOR:-default}"
  log_info "Build directory: $BUILD_DIR"
  log_info "Tests: $([ "$ENABLE_TESTS" = true ] && echo "enabled" || echo "disabled")"
  [ -n "$SANITIZERS" ] && log_info "Sanitizers: $SANITIZERS"
  [ "$CCACHE_ENABLED" = true ] && log_info "CCache: enabled"

  mkdir -p "$BUILD_DIR"

  local cmake_args=""
  cmake_args=$(build_cmake_args)

  log_debug "CMake command: $CMAKE_CMD $cmake_args"

  eval "$CMAKE_CMD $cmake_args"

  # Create symlink to compile_commands.json
  if [ -f "$BUILD_DIR/compile_commands.json" ]; then
    ln -sf "$BUILD_DIR/compile_commands.json" compile_commands.json
    log_debug "Created symlink to compile_commands.json"
  fi

  log_success "Configuration complete!"
}

# Build the project
build_project() {
  log_step "Building project"

  local build_args="--build $BUILD_DIR"

  if [ "$VERBOSE" = true ]; then
    build_args="$build_args --verbose"
  fi

  build_args="$build_args --parallel $PARALLEL_JOBS"

  log_info "Using $PARALLEL_JOBS parallel jobs"

  log_debug "Build command: $CMAKE_CMD $build_args"

  local start_time=""
  local end_time=""
  start_time=$(date +%s)
  $CMAKE_CMD "$build_args"
  end_time=$(date +%s)
  local duration=$((end_time - start_time))

  log_success "Build complete! (took ${duration}s)"
}

# Run tests using run_tests.sh
run_tests() {
  log_step "Running tests"

  if [ ! -f "./run_tests.sh" ]; then
    log_warning "run_tests.sh not found. Skipping tests."
    return
  fi

  if [ ! -x "./run_tests.sh" ]; then
    log_debug "Making run_tests.sh executable"
    chmod +x ./run_tests.sh
  fi

  if [ "$VERBOSE" = true ]; then
    ./run_tests.sh -v
  else
    ./run_tests.sh
  fi

  log_success "Tests complete!"
}

# Install the project
install_project() {
  log_step "Installing project"

  local install_args="--install $BUILD_DIR"

  if [ "$VERBOSE" = true ]; then
    install_args="$install_args --verbose"
  fi

  log_debug "Install command: $CMAKE_CMD $install_args"

  $CMAKE_CMD "$install_args"

  log_success "Installation complete!"
}

# Clean build directory
clean_project() {
  log_step "Cleaning build directory"

  if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
    log_success "Removed $BUILD_DIR"
  else
    log_info "Build directory does not exist, nothing to clean"
  fi

  if [ -L "compile_commands.json" ]; then
    rm -f "compile_commands.json"
    log_debug "Removed compile_commands.json symlink"
  fi

  log_success "Clean complete!"
}

# Main execution
main() {
  parse_args "$@"
  check_dependencies

  case "$COMMAND" in
  build)
    configure_project
    build_project
    [ "$RUN_TESTS" = true ] && run_tests
    ;;
  configure)
    configure_project
    ;;
  clean)
    clean_project
    ;;
  rebuild)
    clean_project
    configure_project
    build_project
    [ "$RUN_TESTS" = true ] && run_tests
    ;;
  test)
    if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
      log_info "Project not configured. Building first..."
      configure_project
      build_project
    fi
    run_tests
    ;;
  install)
    if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
      log_info "Project not configured. Building first..."
      configure_project
      build_project
    fi
    install_project
    ;;
  info)
    show_info
    ;;
  *)
    log_error "Unknown command: $COMMAND"
    print_usage
    exit 1
    ;;
  esac
}

main "$@"
