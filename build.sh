#!/usr/bin/env bash
set -e

# ==============================
# Argument parsing
# ==============================

DEBUG_ONLY=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            DEBUG_ONLY=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
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

C_COMPILER=clang-18
CXX_COMPILER=clang++-18

# Verify compiler exists
if ! command -v $CXX_COMPILER >/dev/null 2>&1; then
    echo "ERROR: clang-18 not found"
    exit 1
fi

echo "Using compiler:"
$CXX_COMPILER --version

COMMON_CMAKE_FLAGS=(
  -DCMAKE_C_COMPILER=$C_COMPILER
  -DCMAKE_CXX_COMPILER=$CXX_COMPILER
  -DCMAKE_BUILD_TYPE=Debug
)

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
