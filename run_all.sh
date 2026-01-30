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
echo " Python Tools Tests"
echo "=============================="

source venv/bin/activate
pytest tests/test_visualize_book.py -v

echo "=============================="
echo " Cross-Validation Tests"
echo "=============================="

# Python cross-validation infrastructure tests
pytest tests/python/ -v

# End-to-end cross-validation: run C++ scenarios and validate with Python
CROSS_VAL_BIN=""
for path in "build/debug/cross_validation_tests" "build/cross_validation_tests"; do
    if [ -x "$path" ]; then
        CROSS_VAL_BIN="$path"
        break
    fi
done

if [ -n "$CROSS_VAL_BIN" ]; then
    echo ""
    echo "Running end-to-end cross-validation with $CROSS_VAL_BIN..."
    CROSS_VAL_DIR=$(mktemp -d)

    # Run C++ test harness - exports state to CROSS_VAL_OUTPUT_DIR
    CROSS_VAL_OUTPUT_DIR="$CROSS_VAL_DIR" "$CROSS_VAL_BIN" \
        --gtest_filter="*Scenario_*"

    # Validate each test's output with Python
    for test_output in "$CROSS_VAL_DIR"/test_*; do
        if [ -d "$test_output/states" ]; then
            echo "Validating: $(basename "$test_output")"
            python -m tools.testing.cross_validator "$test_output"
        fi
    done

    rm -rf "$CROSS_VAL_DIR"
else
    echo "Note: cross_validation_tests binary not found, skipping end-to-end validation"
fi

echo -e "\n\033[32m ALL CHECKS PASSED \033[0m\n"
