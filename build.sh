#!/bin/bash

set -e

BUILD_DIR="build"
CMAKE_CMD="cmake"
GENERATOR="Ninja"

# Check if ninja is available
if ! command -v ninja &> /dev/null; then
  echo "Warning: ninja not found. Install it with:"
  echo "  Fedora/RHEL: sudo dnf install ninja-build"
  echo "  Ubuntu/Debian: sudo apt install ninja-build"
  echo "  macOS: brew install ninja"
  echo ""
  echo "Falling back to default CMake generator..."
  GENERATOR=""
fi

# Build CMake arguments
CMAKE_ARGS="-S . -B $BUILD_DIR"
if [ -n "$GENERATOR" ]; then
  CMAKE_ARGS="$CMAKE_ARGS -G $GENERATOR"
fi

# Parse command line arguments
case "${1:-build}" in
  build)
    echo "Configuring and building project with ${GENERATOR:-default generator}..."
    mkdir -p "$BUILD_DIR"
    $CMAKE_CMD $CMAKE_ARGS

    # Create symlink to compile_commands.json
    if [ -f "$BUILD_DIR/compile_commands.json" ]; then
      ln -sf "$BUILD_DIR/compile_commands.json" compile_commands.json
    fi

    $CMAKE_CMD --build "$BUILD_DIR"
    echo "Build complete!"
    ;;
  configure)
    echo "Configuring project with ${GENERATOR:-default generator}..."
    mkdir -p "$BUILD_DIR"
    $CMAKE_CMD $CMAKE_ARGS

    # Create symlink to compile_commands.json
    if [ -f "$BUILD_DIR/compile_commands.json" ]; then
      ln -sf "$BUILD_DIR/compile_commands.json" compile_commands.json
    fi
    echo "Configuration complete!"
    ;;
  clean)
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo "Clean complete!"
    ;;
  rebuild)
    echo "Rebuilding project with ${GENERATOR:-default generator}..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    $CMAKE_CMD $CMAKE_ARGS

    # Create symlink to compile_commands.json
    if [ -f "$BUILD_DIR/compile_commands.json" ]; then
      ln -sf "$BUILD_DIR/compile_commands.json" compile_commands.json
    fi

    $CMAKE_CMD --build "$BUILD_DIR"
    echo "Rebuild complete!"
    ;;
  *)
    echo "Usage: $0 [build|configure|clean|rebuild]"
    echo ""
    echo "Commands:"
    echo "  build      - Configure and build the project (default)"
    echo "  configure  - Configure CMake only"
    echo "  clean      - Remove build directory"
    echo "  rebuild    - Clean and rebuild"
    exit 1
    ;;
esac

