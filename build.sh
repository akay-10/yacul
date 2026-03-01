#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
LOGS_DIR="${PROJECT_ROOT}/logs"
BUILD_TYPE="Debug"
BUILD_TESTS="ON"
GENERATOR="Unix Makefiles"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
CLEAN_LOGS=false

# Terminal colors
if [ -t 1 ]; then
  RED='\033[0;31m'
  GREEN='\033[0;32m'
  YELLOW='\033[1;33m'
  BLUE='\033[0;34m'
  MAGENTA='\033[0;35m'
  CYAN='\033[0;36m'
  BOLD='\033[1m'
  RESET='\033[0m'
else
  RED=''
  GREEN=''
  YELLOW=''
  BLUE=''
  MAGENTA=''
  CYAN=''
  BOLD=''
  RESET=''
fi

# Check if ninja is available
if command -v ninja &>/dev/null; then
  GENERATOR="Ninja"
fi

# Logging functions
info() {
  echo -e "${GREEN}➜${RESET} $*"
}

success() {
  echo -e "${BOLD}${GREEN}✓${RESET} $*"
}

warn() {
  echo -e "${YELLOW}⚠${RESET} $*"
}

error() {
  echo -e "${BOLD}${RED}✗${RESET} $*" >&2
}

header() {
  echo -e "${BOLD}${BLUE}$*${RESET}"
}

label() {
  echo -e "${CYAN}$1${RESET} $2"
}

value() {
  echo -e "${MAGENTA}$*${RESET}"
}

usage() {
  echo -e "${BOLD}Usage:${RESET} $(basename "$0") [COMMAND] [OPTIONS]"
  echo ""
  echo -e "${BOLD}COMMANDS:${RESET}"
  echo -e "    ${GREEN}configure${RESET}       Configure the build system"
  echo -e "    ${GREEN}build${RESET}           Build the project (default)"
  echo -e "    ${GREEN}rebuild${RESET}         Clean and build"
  echo -e "    ${GREEN}clean${RESET}           Remove build directory"
  echo -e "    ${GREEN}test${RESET}            Run tests"
  echo -e "    ${GREEN}info${RESET}            Show build information"
  echo ""
  echo -e "${BOLD}OPTIONS:${RESET}"
  echo -e "    ${CYAN}-d, --debug${RESET}     Build with debug symbols (default)"
  echo -e "    ${CYAN}-r, --release${RESET}   Build in release mode (tests disabled)"
  echo -e "    ${CYAN}-g${RESET}              Enable debug symbols"
  echo -e "    ${CYAN}-j N${RESET}            Use N parallel jobs (default: ${JOBS})"
  echo -e "    ${CYAN}--make${RESET}          Force use of Make instead of Ninja"
  echo -e "    ${CYAN}--tests-on${RESET}      Force enable tests"
  echo -e "    ${CYAN}--tests-off${RESET}     Force disable tests"
  echo -e "    ${CYAN}--logs${RESET}          Delete all logs. Can be used only with clean COMMAND"
  echo -e "    ${CYAN}-h, --help${RESET}      Show this help message"
  echo ""
  echo -e "${BOLD}EXAMPLES:${RESET}"
  echo -e "    ${YELLOW}$(basename "$0")${RESET}                    # Build in debug mode"
  echo -e "    ${YELLOW}$(basename "$0") -r${RESET}                 # Build in release mode"
  echo -e "    ${YELLOW}$(basename "$0") rebuild -g -j 8${RESET}    # Rebuild with symbols using 8 jobs"
  echo -e "    ${YELLOW}$(basename "$0") clean${RESET}              # Clean build directory"
  echo -e "    ${YELLOW}$(basename "$0") clean --logs${RESET}       # Clean logs directory"
  echo -e "    ${YELLOW}$(basename "$0") configure --make${RESET}   # Configure with Make generator"
  echo ""
  exit 0
}

show_info() {
  header "==================================="
  header "Build Information"
  header "==================================="
  label "Project Root:" "   ${PROJECT_ROOT}"
  label "Build Directory:" "${BUILD_DIR}"
  echo -e "${CYAN}Build Type:${RESET}      $(value ${BUILD_TYPE})"
  echo -e "${CYAN}Build Tests:${RESET}     $(value ${BUILD_TESTS})"
  echo -e "${CYAN}Generator:${RESET}       $(value ${GENERATOR})"
  echo -e "${CYAN}Parallel Jobs:${RESET}   $(value ${JOBS})"
  header "==================================="
}

configure() {
  info "Configuring project..."
  mkdir -p "${BUILD_DIR}"

  cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_TEST="${BUILD_TESTS}"

  # Create symbolic link for compile_commands.json
  if [ -f "${BUILD_DIR}/compile_commands.json" ]; then
    ln -sf "${BUILD_DIR}/compile_commands.json" "${PROJECT_ROOT}/compile_commands.json"
    success "Created symlink: ${CYAN}compile_commands.json${RESET} -> ${BUILD_DIR}/compile_commands.json"
  fi

  success "Configuration complete!"
}

build() {
  if [ ! -d "${BUILD_DIR}" ]; then
    warn "Build directory not found. Configuring first..."
    configure
  fi

  info "Building project..."
  cmake --build "${BUILD_DIR}" -j "${JOBS}"
  success "Build complete!"
}

clean() {
  if [ "$CLEAN_LOGS" = true ]; then
    info "Cleaning logs directory..."
    rm -rf "${LOGS_DIR}"
  else
    info "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    rm -f "${PROJECT_ROOT}/compile_commands.json"
  fi
  success "Clean complete!"
}

rebuild() {
  header "Rebuilding project..."
  clean
  configure
  build
}

run_tests() {
  if [ ! -d "${BUILD_DIR}" ]; then
    warn "Build directory not found. Building first..."
    build
  fi

  if [ "${BUILD_TESTS}" != "ON" ]; then
    error "Tests are disabled in this build configuration."
    exit 1
  fi

  if [ ! -f "${PROJECT_ROOT}/run_tests.sh" ]; then
    error "run_tests.sh not found in project root"
    exit 1
  fi

  info "Running tests..."
  "${PROJECT_ROOT}/run_tests.sh"
}

parse_options() {
  # Parse command
  COMMAND="build"
  if [ $# -gt 0 ] && [[ ! "$1" =~ ^- ]]; then
    COMMAND="$1"
    shift
  fi

  # Parse options
  while [ $# -gt 0 ]; do
    case "$1" in
    -d | --debug)
      BUILD_TYPE="Debug"
      BUILD_TESTS="ON"
      ;;
    -r | --release)
      BUILD_TYPE="Release"
      BUILD_TESTS="OFF"
      ;;
    -g)
      # Debug symbols are included by default in Debug mode
      # For Release mode with symbols, use RelWithDebInfo
      if [ "${BUILD_TYPE}" = "Release" ]; then
        BUILD_TYPE="RelWithDebInfo"
      fi
      ;;
    -j)
      shift
      JOBS="$1"
      ;;
    --make)
      GENERATOR="Unix Makefiles"
      ;;
    --tests-on)
      BUILD_TESTS="ON"
      ;;
    --tests-off)
      BUILD_TESTS="OFF"
      ;;
    --logs)
      CLEAN_LOGS=true
      ;;
    -h | --help)
      usage
      ;;
    *)
      error "Unknown option: $1"
      usage
      ;;
    esac
    shift
  done
}

execute_command() {
  case "${COMMAND}" in
  configure)
    configure
    ;;
  build)
    build
    ;;
  rebuild)
    rebuild
    ;;
  clean)
    clean
    ;;
  test)
    run_tests
    ;;
  info)
    show_info
    ;;
  *)
    error "Unknown command: ${COMMAND}"
    usage
    ;;
  esac
}

main() {
  parse_options "$@"
  execute_command
}

main "$@"
