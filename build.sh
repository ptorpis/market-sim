#!/usr/bin/env bash
set -e

# ==============================
# Argument parsing
# ==============================

DEBUG_ONLY=false
TOOLCHAIN=""
RUN_VALGRIND=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            DEBUG_ONLY=true
            shift
            ;;
        --gcc)
            [ -n "$TOOLCHAIN" ] && TOOLCHAIN=both || TOOLCHAIN=gcc
            shift
            ;;
        --clang)
            [ -n "$TOOLCHAIN" ] && TOOLCHAIN=both || TOOLCHAIN=clang
            shift
            ;;
        --valgrind)
            RUN_VALGRIND=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --gcc      Use GCC toolchain (g++)"
            echo "  --clang    Use Clang toolchain (clang++)"
            echo "  --valgrind Include Valgrind memcheck build and test"
            echo "  --debug    Only build and test the debug configuration"
            echo "  -h, --help Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

if [ -z "$TOOLCHAIN" ]; then
    echo "ERROR: must specify --gcc or --clang"
    echo "Use --help for usage information"
    exit 1
fi

if [ "$TOOLCHAIN" = "both" ]; then
    echo "ERROR: cannot specify both --gcc and --clang"
    echo "Use --help for usage information"
    exit 1
fi

# ==============================
# Build configuration
# ==============================

CPU_COUNT=$(nproc)
BUILD_JOBS=$((CPU_COUNT - 2))

if [ "$BUILD_JOBS" -lt 1 ]; then
    BUILD_JOBS=1
fi

echo "Using $BUILD_JOBS parallel build jobs"

# ==============================
# Toolchain selection
# ==============================

GCC_INSTALL_DIR=$(g++ -print-search-dirs | head -1 | sed 's/install: //')

if [ "$TOOLCHAIN" = "gcc" ]; then
    C_COMPILER=gcc
    CXX_COMPILER=g++
else
    C_COMPILER=clang
    CXX_COMPILER=clang++
fi

# Verify compiler exists
if ! command -v $CXX_COMPILER >/dev/null 2>&1; then
    echo "ERROR: $CXX_COMPILER not found"
    exit 1
fi

echo "Using compiler:"
$CXX_COMPILER --version

COMMON_CMAKE_FLAGS=(
  -DCMAKE_C_COMPILER=$C_COMPILER
  -DCMAKE_CXX_COMPILER=$CXX_COMPILER
  -DCMAKE_BUILD_TYPE=Debug
)

if [ "$TOOLCHAIN" = "clang" ]; then
    COMMON_CMAKE_FLAGS+=(
      -DCMAKE_CXX_FLAGS="--gcc-install-dir=$GCC_INSTALL_DIR"
      -DCMAKE_EXE_LINKER_FLAGS="--gcc-install-dir=$GCC_INSTALL_DIR"
    )
fi

# ==============================
# Normal Debug Build + Tests
# ==============================

echo "=============================="
echo " Normal Debug Build + Tests"
echo "=============================="

cmake -S . -B build/debug "${COMMON_CMAKE_FLAGS[@]}"
cmake --build build/debug --parallel $BUILD_JOBS
ctest --test-dir build/debug --output-on-failure

if [ "$DEBUG_ONLY" = true ]; then
    echo -e "\n\033[32m DEBUG BUILD PASSED \033[0m\n"
    exit 0
fi

# ==============================
# Sanitizer Build (ASAN+UBSAN)
# ==============================

echo "=============================="
echo " Sanitizer Build (ASAN+UBSAN)"
echo "=============================="

cmake -S . -B build/asan \
  "${COMMON_CMAKE_FLAGS[@]}" \
  -DENABLE_ASAN=ON \
  -DENABLE_UBSAN=ON

cmake --build build/asan --parallel $BUILD_JOBS
ctest --test-dir build/asan --output-on-failure

# ==============================
# Valgrind Build + Tests
# ==============================

if [ "$RUN_VALGRIND" = true ]; then
    echo "=============================="
    echo " Valgrind Build + Tests"
    echo "=============================="

    cmake -S . -B build/valgrind \
      "${COMMON_CMAKE_FLAGS[@]}" \
      -DENABLE_VALGRIND=ON

    cmake --build build/valgrind --parallel $BUILD_JOBS
    ctest --test-dir build/valgrind --output-on-failure
fi

# ==============================
# Python Tools Tests
# ==============================

echo "=============================="
echo " Python Tools Tests"
echo "=============================="

source venv/bin/activate
pytest tests/test_visualize_book.py -v

echo "=============================="
echo " Cross-Validation Tests"
echo "=============================="

pytest tests/python/ -v

python -m tools.testing.harness --build-dir build/debug

echo "=============================="
echo " Adverse Selection Tests"
echo "=============================="

pytest tests/test_adverse_selection.py -v

echo -e "\n\033[32m ALL CHECKS PASSED \033[0m\n"
