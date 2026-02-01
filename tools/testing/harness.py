"""
Cross-validation test harness.

Orchestrates end-to-end cross-validation testing:
1. Discovers and runs C++ cross_validation_tests binary
2. Collects test output directories
3. Runs Python validation for each test
4. Reports results with detailed diagnostics

Usage:
    python -m tools.testing.harness
    python -m tools.testing.harness --build-dir build/debug
    python -m tools.testing.harness --verbose --keep-output
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Iterator

from tools.testing.cross_validator import CrossValidator


class ValidationStatus(Enum):
    """Status of a cross-validation test."""

    PASSED = "passed"
    FAILED = "failed"
    SKIPPED = "skipped"
    ERROR = "error"


@dataclass
class ValidationResult:
    """Result of a single cross-validation test."""

    name: str
    status: ValidationStatus
    cpp_passed: bool = True
    python_passed: bool = True
    state_comparisons: int = 0
    state_failures: int = 0
    error_message: str = ""
    differences: list[str] = field(default_factory=list)

    def __str__(self) -> str:
        status_icons = {
            ValidationStatus.PASSED: "\033[32m[PASS]\033[0m",
            ValidationStatus.FAILED: "\033[31m[FAIL]\033[0m",
            ValidationStatus.SKIPPED: "\033[33m[SKIP]\033[0m",
            ValidationStatus.ERROR: "\033[31m[ERR]\033[0m",
        }
        icon = status_icons.get(self.status, "[???]")
        result = f"{icon} {self.name}"

        if self.status == ValidationStatus.PASSED:
            result += f" ({self.state_comparisons} states validated)"
        elif self.status == ValidationStatus.FAILED:
            result += f" ({self.state_failures}/{self.state_comparisons} states failed)"
        elif self.error_message:
            result += f": {self.error_message}"

        return result


@dataclass
class HarnessResult:
    """Aggregate result of all cross-validation tests."""

    tests: list[ValidationResult] = field(default_factory=list)
    cpp_binary: str = ""
    output_dir: str = ""

    @property
    def total(self) -> int:
        return len(self.tests)

    @property
    def passed(self) -> int:
        return sum(1 for t in self.tests if t.status == ValidationStatus.PASSED)

    @property
    def failed(self) -> int:
        return sum(1 for t in self.tests if t.status == ValidationStatus.FAILED)

    @property
    def errors(self) -> int:
        return sum(1 for t in self.tests if t.status == ValidationStatus.ERROR)

    @property
    def skipped(self) -> int:
        return sum(1 for t in self.tests if t.status == ValidationStatus.SKIPPED)

    @property
    def success(self) -> bool:
        return self.failed == 0 and self.errors == 0

    def summary(self) -> str:
        lines = [
            "",
            "=" * 60,
            "Cross-Validation Summary",
            "=" * 60,
            f"Binary: {self.cpp_binary}",
            f"Total tests: {self.total}",
            f"  Passed: {self.passed}",
            f"  Failed: {self.failed}",
            f"  Errors: {self.errors}",
            f"  Skipped: {self.skipped}",
            "",
        ]

        if self.success:
            lines.append("\033[32mALL CROSS-VALIDATION TESTS PASSED\033[0m")
        else:
            lines.append("\033[31mCROSS-VALIDATION FAILED\033[0m")

        return "\n".join(lines)


class CrossValidationHarness:
    """
    Orchestrates cross-validation between C++ and Python implementations.

    The harness:
    1. Locates the C++ cross_validation_tests binary
    2. Runs scenario tests with state export enabled
    3. For each test's output, runs Python validation
    4. Collects and reports results
    """

    # Standard locations to search for the binary
    BINARY_SEARCH_PATHS = [
        "build/debug/cross_validation_tests",
        "build/release/cross_validation_tests",
        "build/asan/cross_validation_tests",
        "build/cross_validation_tests",
    ]

    # GTest filter for scenario tests that export state
    SCENARIO_FILTER = "*Scenario_*"

    def __init__(
        self,
        build_dir: Path | None = None,
        keep_output: bool = False,
        verbose: bool = False,
    ):
        """
        Initialize the harness.

        Args:
            build_dir: Explicit build directory containing the binary.
                       If None, searches standard locations.
            keep_output: If True, preserve test output directories.
            verbose: If True, print detailed progress information.
        """
        self.build_dir = build_dir
        self.keep_output = keep_output
        self.verbose = verbose
        self._output_dir: Path | None = None

    def find_binary(self) -> Path | None:
        """
        Locate the cross_validation_tests binary.

        If an explicit build_dir is provided, only searches there.
        Otherwise, searches standard locations.

        Returns:
            Path to the binary, or None if not found.
        """
        # If explicit build dir provided, only look there
        if self.build_dir:
            candidate = self.build_dir / "cross_validation_tests"
            if candidate.exists() and os.access(candidate, os.X_OK):
                return candidate
            return None

        # Search standard locations
        for path_str in self.BINARY_SEARCH_PATHS:
            candidate = Path(path_str)
            if candidate.exists() and os.access(candidate, os.X_OK):
                return candidate

        return None

    def _run_cpp_tests(self, binary: Path, output_dir: Path) -> tuple[bool, str]:
        """
        Run C++ cross-validation tests with state export.

        Args:
            binary: Path to the cross_validation_tests binary.
            output_dir: Directory for test output.

        Returns:
            Tuple of (success, stderr_output)
        """
        env = os.environ.copy()
        env["CROSS_VAL_OUTPUT_DIR"] = str(output_dir)

        cmd = [
            str(binary),
            f"--gtest_filter={self.SCENARIO_FILTER}",
        ]

        if self.verbose:
            print(f"Running: {' '.join(cmd)}")
            print(f"Output dir: {output_dir}")

        try:
            result = subprocess.run(
                cmd,
                env=env,
                capture_output=True,
                text=True,
                timeout=300,  # 5 minute timeout
                check=False,  # We handle return code manually
            )

            if self.verbose and result.stdout:
                print(result.stdout)

            return result.returncode == 0, result.stderr

        except subprocess.TimeoutExpired:
            return False, "C++ tests timed out after 5 minutes"
        except OSError as e:
            return False, f"Failed to run C++ tests: {e}"

    def _discover_test_outputs(self, output_dir: Path) -> Iterator[Path]:
        """
        Discover test output directories.

        Each test creates a subdirectory like test_0, test_1, etc.

        Args:
            output_dir: Root output directory.

        Yields:
            Paths to individual test output directories.
        """
        if not output_dir.exists():
            return

        for entry in sorted(output_dir.iterdir()):
            if entry.is_dir() and entry.name.startswith("test_"):
                # Must have a states subdirectory to be valid
                states_dir = entry / "states"
                if states_dir.exists() and any(states_dir.glob("state_*.json")):
                    yield entry

    def _validate_test_output(self, test_dir: Path) -> ValidationResult:
        """
        Validate a single test's output using Python cross-validator.

        Args:
            test_dir: Directory containing deltas.csv, trades.csv, and states/.

        Returns:
            ValidationResult with validation outcome.
        """
        test_name = test_dir.name

        # Check required files exist
        deltas_file = test_dir / "deltas.csv"
        states_dir = test_dir / "states"

        if not deltas_file.exists():
            return ValidationResult(
                name=test_name,
                status=ValidationStatus.ERROR,
                error_message="Missing deltas.csv",
            )

        if not states_dir.exists():
            return ValidationResult(
                name=test_name,
                status=ValidationStatus.ERROR,
                error_message="Missing states/ directory",
            )

        # Run cross-validation
        try:
            validator = CrossValidator(output_dir=test_dir)
            results = list(validator.validate_all())

            if not results:
                return ValidationResult(
                    name=test_name,
                    status=ValidationStatus.ERROR,
                    error_message="No state files to validate",
                )

            # Count passes and failures
            failures = [r for r in results if not r.match]
            all_diffs = []
            for r in failures:
                all_diffs.extend(r.differences[:5])  # First 5 diffs per failure

            if failures:
                return ValidationResult(
                    name=test_name,
                    status=ValidationStatus.FAILED,
                    state_comparisons=len(results),
                    state_failures=len(failures),
                    differences=all_diffs[:20],  # Cap total diffs
                )

            return ValidationResult(
                name=test_name,
                status=ValidationStatus.PASSED,
                state_comparisons=len(results),
            )

        except Exception as e:
            return ValidationResult(
                name=test_name,
                status=ValidationStatus.ERROR,
                error_message=str(e),
            )

    def run(self) -> HarnessResult:
        """
        Run the full cross-validation test suite.

        Returns:
            HarnessResult with all test outcomes.
        """
        result = HarnessResult()

        # Find binary
        binary = self.find_binary()
        if binary is None:
            print("\033[31mError: cross_validation_tests binary not found\033[0m")
            print("Searched locations:")
            for path in self.BINARY_SEARCH_PATHS:
                print(f"  - {path}")
            if self.build_dir:
                print(f"  - {self.build_dir / 'cross_validation_tests'}")
            return result

        result.cpp_binary = str(binary)

        # Create output directory
        if self.keep_output:
            self._output_dir = Path(tempfile.mkdtemp(prefix="cross_val_"))
            print(f"Output directory: {self._output_dir}")
        else:
            self._output_dir = Path(tempfile.mkdtemp(prefix="cross_val_"))

        result.output_dir = str(self._output_dir)

        try:
            # Run C++ tests
            print("\n" + "=" * 60)
            print("Running C++ cross-validation tests...")
            print("=" * 60)

            cpp_success, cpp_stderr = self._run_cpp_tests(binary, self._output_dir)

            if not cpp_success:
                print("\033[31mC++ tests failed\033[0m")
                if cpp_stderr:
                    print(f"stderr: {cpp_stderr[:500]}")
                result.tests.append(
                    ValidationResult(
                        name="cpp_execution",
                        status=ValidationStatus.ERROR,
                        cpp_passed=False,
                        error_message=cpp_stderr[:200] if cpp_stderr else "Unknown error",
                    )
                )
                return result

            # Discover and validate test outputs
            print("\n" + "=" * 60)
            print("Validating test outputs with Python...")
            print("=" * 60)

            test_dirs = list(self._discover_test_outputs(self._output_dir))

            if not test_dirs:
                print("\033[33mWarning: No test outputs found\033[0m")
                result.tests.append(
                    ValidationResult(
                        name="discovery",
                        status=ValidationStatus.SKIPPED,
                        error_message="No test output directories found",
                    )
                )
                return result

            # Validate each test
            for test_dir in test_dirs:
                if self.verbose:
                    print(f"\nValidating {test_dir.name}...")

                test_result = self._validate_test_output(test_dir)
                result.tests.append(test_result)
                print(test_result)

                # Print differences for failures
                if test_result.status == ValidationStatus.FAILED and test_result.differences:
                    print("  Differences:")
                    for diff in test_result.differences[:5]:
                        print(f"    - {diff}")
                    if len(test_result.differences) > 5:
                        print(f"    ... and {len(test_result.differences) - 5} more")

        finally:
            # Cleanup unless keeping output
            if not self.keep_output and self._output_dir and self._output_dir.exists():
                shutil.rmtree(self._output_dir)

        return result


def main() -> int:
    """CLI entry point."""
    parser = argparse.ArgumentParser(
        description="Cross-validation test harness for market simulator"
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="Build directory containing cross_validation_tests binary",
    )
    parser.add_argument(
        "--keep-output",
        action="store_true",
        help="Preserve test output directories for inspection",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Print detailed progress information",
    )

    args = parser.parse_args()

    harness = CrossValidationHarness(
        build_dir=args.build_dir,
        keep_output=args.keep_output,
        verbose=args.verbose,
    )

    result = harness.run()
    print(result.summary())

    return 0 if result.success else 1


if __name__ == "__main__":
    sys.exit(main())
