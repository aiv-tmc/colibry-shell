#!/usr/bin/env bash
# Technical build script for the Colibry compositor.
#
# Configures and builds the project with CMake into a local build directory.
# Nothing is installed and no system files are touched.
#
# Usage:
#   ./build.sh                  # configure (if needed) and build
#   ./build.sh --clean          # remove the build directory first
#   ./build.sh --type Debug     # CMake build type (default: Debug)
#   ./build.sh --asan           # build with AddressSanitizer + UBSan
#   ./build.sh --verbose        # show full compiler command lines
#   ./build.sh --jobs N         # parallel build jobs (default: nproc)
set -euo pipefail

# Out-of-source build directory relative to the repository root.
BUILD_DIR="build"

# CMake build type; Debug keeps symbols and asserts for development.
BUILD_TYPE="Debug"

# Extra CMake defines collected from options.
EXTRA_DEFINES=()

# Number of parallel compiler jobs.
JOBS="$(nproc 2>/dev/null || echo 4)"

# Whether the build directory is removed before configuring.
DO_CLEAN=0

# Whether the build runs in verbose mode.
VERBOSE=0

# Prints every compiler invocation when enabled.
CMAKE_VERBOSE="OFF"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            DO_CLEAN=1
            shift
            ;;
        --type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        --asan)
            # Sanitizer flags applied to both compilation and linking.
            EXTRA_DEFINES+=("-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer")
            EXTRA_DEFINES+=("-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined")
            shift
            ;;
        --verbose)
            VERBOSE=1
            CMAKE_VERBOSE="ON"
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            grep '^#' "$0" | head -n 13
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Resolves the repository root so the script works from any directory.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# Verifies that the build tools are present before doing any work.
for tool in cmake pkg-config; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: '$tool' is required but not installed" >&2
        exit 1
    fi
done

# Verifies that all pkg-config dependencies resolve before configuring.
# pixman-1 is required because hyprutils math headers include pixman.h.
for dep in aquamarine hyprutils hyprlang hyprgraphics wayland-server libdrm gbm glesv2 egl pixman-1; do
    if ! pkg-config --exists "$dep"; then
        echo "error: missing pkg-config dependency '$dep'" >&2
        exit 1
    fi
done

# Removes previous artifacts when a clean build is requested.
if [[ "$DO_CLEAN" -eq 1 ]]; then
    rm -rf "$BUILD_DIR"
fi

# Configures only when the directory is missing or CMake files changed.
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" || "$DO_CLEAN" -eq 1 ]]; then
    cmake -S . -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DCMAKE_VERBOSE_MAKEFILE="$CMAKE_VERBOSE" \
        "${EXTRA_DEFINES[@]}"
fi

# Builds the project; --verbose is forwarded to the underlying generator.
BUILD_ARGS=(--build "$BUILD_DIR" --parallel "$JOBS")
[[ "$VERBOSE" -eq 1 ]] && BUILD_ARGS+=(--verbose)
cmake "${BUILD_ARGS[@]}"

echo
echo "built: $ROOT_DIR/$BUILD_DIR/colibry"
echo "       $ROOT_DIR/$BUILD_DIR/colibryctl"
echo "run nested inside a Wayland session:  $BUILD_DIR/colibry"
