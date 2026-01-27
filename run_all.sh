##!/usr/bin/env bash
set -e

CPU_COUNT=$(nproc)
BUILD_JOBS=$((CPU_COUNT - 2))

if [ "$BUILD_JOBS" -lt 1 ]; then
    BUILD_JOBS=1
fi

echo "Using $BUILD_JOBS parallel build jobs"

echo "=============================="
echo " Normal Debug Build + Tests"
echo "=============================="

cmake -S . -B build/debug
cmake --build build/debug --parallel $BUILD_JOBS
ctest --test-dir build/debug --output-on-failure

echo "=============================="
echo " Sanitizer Build (ASAN+UBSAN)"
echo "=============================="

cmake -S . -B build/asan \
  -DENABLE_ASAN=ON \
  -DENABLE_UBSAN=ON

cmake --build build/asan --parallel $BUILD_JOBS
ctest --test-dir build/asan --output-on-failure

echo "=============================="
echo " Valgrind Build"
echo "=============================="

cmake -S . -B build/valgrind \
  -DENABLE_VALGRIND=ON

cmake --build build/valgrind --parallel $BUILD_JOBS
ctest --test-dir build/valgrind --output-on-failure

echo "=============================="
echo " ALL CHECKS PASSED"
echo "=============================="
