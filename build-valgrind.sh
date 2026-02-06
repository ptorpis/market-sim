#!/usr/bin/env bash
set -e

# ==============================
# Valgrind Build + Tests
# ==============================
# Separate from main pipeline due to ~10-50x slowdown.
# ASan+UBSan in build.sh catches most of the same issues.
# Run this occasionally for uninitialized memory read detection.

CPU_COUNT=$(nproc)
BUILD_JOBS=$((CPU_COUNT - 2))

if [ "$BUILD_JOBS" -lt 1 ]; then
    BUILD_JOBS=1
fi

C_COMPILER=clang-18
CXX_COMPILER=clang++-18

if ! command -v $CXX_COMPILER >/dev/null 2>&1; then
    echo "ERROR: clang-18 not found"
    exit 1
fi

echo "=============================="
echo " Valgrind Build"
echo "=============================="

cmake -S . -B build/valgrind \
  -DCMAKE_C_COMPILER=$C_COMPILER \
  -DCMAKE_CXX_COMPILER=$CXX_COMPILER \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_VALGRIND=ON

cmake --build build/valgrind --parallel $BUILD_JOBS
ctest --test-dir build/valgrind --output-on-failure

echo -e "\n\033[32m VALGRIND CHECKS PASSED \033[0m\n"
