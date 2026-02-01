"""
Tests for the cross-validation harness.
"""

import json

from tools.testing.harness import (
    CrossValidationHarness,
    HarnessResult,
    ValidationResult,
    ValidationStatus,
)


class TestValidationResult:
    """Tests for ValidationResult dataclass."""

    def test_passed_result_str(self):
        """Passed result should show green PASS and state count."""
        result = ValidationResult(
            name="test_basic",
            status=ValidationStatus.PASSED,
            state_comparisons=5,
        )
        s = str(result)
        assert "PASS" in s
        assert "test_basic" in s
        assert "5 states" in s

    def test_failed_result_str(self):
        """Failed result should show red FAIL and failure count."""
        result = ValidationResult(
            name="test_broken",
            status=ValidationStatus.FAILED,
            state_comparisons=10,
            state_failures=3,
        )
        s = str(result)
        assert "FAIL" in s
        assert "test_broken" in s
        assert "3/10" in s

    def test_error_result_str(self):
        """Error result should show error message."""
        result = ValidationResult(
            name="test_crash",
            status=ValidationStatus.ERROR,
            error_message="Binary not found",
        )
        s = str(result)
        assert "ERR" in s
        assert "Binary not found" in s


class TestHarnessResult:
    """Tests for HarnessResult dataclass."""

    def test_empty_result_is_success(self):
        """Empty result (no tests) is technically a success."""
        result = HarnessResult()
        assert result.success
        assert result.total == 0

    def test_all_passed_is_success(self):
        """All passed tests means success."""
        result = HarnessResult(
            tests=[
                ValidationResult(name="test_1", status=ValidationStatus.PASSED),
                ValidationResult(name="test_2", status=ValidationStatus.PASSED),
            ]
        )
        assert result.success
        assert result.passed == 2
        assert result.failed == 0

    def test_any_failure_is_not_success(self):
        """Any failed test means not success."""
        result = HarnessResult(
            tests=[
                ValidationResult(name="test_1", status=ValidationStatus.PASSED),
                ValidationResult(name="test_2", status=ValidationStatus.FAILED),
            ]
        )
        assert not result.success
        assert result.passed == 1
        assert result.failed == 1

    def test_any_error_is_not_success(self):
        """Any error means not success."""
        result = HarnessResult(
            tests=[
                ValidationResult(name="test_1", status=ValidationStatus.PASSED),
                ValidationResult(name="test_2", status=ValidationStatus.ERROR),
            ]
        )
        assert not result.success
        assert result.errors == 1

    def test_skipped_does_not_affect_success(self):
        """Skipped tests don't affect success status."""
        result = HarnessResult(
            tests=[
                ValidationResult(name="test_1", status=ValidationStatus.PASSED),
                ValidationResult(name="test_2", status=ValidationStatus.SKIPPED),
            ]
        )
        assert result.success
        assert result.skipped == 1

    def test_summary_contains_counts(self):
        """Summary should contain test counts."""
        result = HarnessResult(
            tests=[
                ValidationResult(name="test_1", status=ValidationStatus.PASSED),
                ValidationResult(name="test_2", status=ValidationStatus.FAILED),
            ],
            cpp_binary="/path/to/binary",
        )
        summary = result.summary()
        assert "Total tests: 2" in summary
        assert "Passed: 1" in summary
        assert "Failed: 1" in summary


class TestCrossValidationHarness:
    """Tests for CrossValidationHarness class."""

    def test_find_binary_explicit_path(self, tmp_path):
        """Should find binary in explicit build directory."""
        # Create a fake binary
        build_dir = tmp_path / "build"
        build_dir.mkdir()
        binary = build_dir / "cross_validation_tests"
        binary.touch()
        binary.chmod(0o755)

        harness = CrossValidationHarness(build_dir=build_dir)
        found = harness.find_binary()

        assert found == binary

    def test_find_binary_not_found(self, tmp_path):
        """Should return None when binary not found."""
        harness = CrossValidationHarness(build_dir=tmp_path)
        found = harness.find_binary()

        assert found is None

    def test_discover_test_outputs(self, tmp_path):
        """Should discover test output directories with states."""
        # Create test_0 with states
        test_0 = tmp_path / "test_0"
        test_0.mkdir()
        (test_0 / "states").mkdir()
        (test_0 / "states" / "state_000001.json").write_text("{}")

        # Create test_1 with states
        test_1 = tmp_path / "test_1"
        test_1.mkdir()
        (test_1 / "states").mkdir()
        (test_1 / "states" / "state_000001.json").write_text("{}")

        # Create test_2 without states (should be skipped)
        test_2 = tmp_path / "test_2"
        test_2.mkdir()

        # Create non-test directory (should be skipped)
        other = tmp_path / "other"
        other.mkdir()

        harness = CrossValidationHarness()
        harness._output_dir = tmp_path

        discovered = list(harness._discover_test_outputs(tmp_path))

        assert len(discovered) == 2
        assert test_0 in discovered
        assert test_1 in discovered

    def test_validate_test_output_missing_deltas(self, tmp_path):
        """Should return error when deltas.csv is missing."""
        test_dir = tmp_path / "test_0"
        test_dir.mkdir()
        (test_dir / "states").mkdir()

        harness = CrossValidationHarness()
        result = harness._validate_test_output(test_dir)

        assert result.status == ValidationStatus.ERROR
        assert "deltas.csv" in result.error_message

    def test_validate_test_output_missing_states(self, tmp_path):
        """Should return error when states directory is missing."""
        test_dir = tmp_path / "test_0"
        test_dir.mkdir()
        (test_dir / "deltas.csv").write_text("timestamp,delta_type\n")

        harness = CrossValidationHarness()
        result = harness._validate_test_output(test_dir)

        assert result.status == ValidationStatus.ERROR
        assert "states" in result.error_message

    def test_validate_test_output_success(self, tmp_path):
        """Should return PASSED when validation succeeds."""
        test_dir = tmp_path / "test_0"
        test_dir.mkdir()
        states_dir = test_dir / "states"
        states_dir.mkdir()

        # Write deltas.csv with one ADD
        deltas = """timestamp,sequence_num,delta_type,order_id,client_id,instrument_id,side,price,quantity,remaining_qty,trade_id,new_order_id,new_price,new_quantity
100,0,ADD,1,100,1,BUY,1000,50,50,0,0,0,0
"""
        (test_dir / "deltas.csv").write_text(deltas)
        (test_dir / "trades.csv").write_text(
            "timestamp,trade_id,instrument_id,buyer_id,seller_id,buyer_order_id,seller_order_id,price,quantity\n"
        )

        # Write matching state
        state = {
            "timestamp": 100,
            "sequence_num": 1,
            "order_books": {
                "1": {
                    "bids": [
                        {
                            "price": 1000,
                            "orders": [
                                {
                                    "order_id": 1,
                                    "client_id": 100,
                                    "quantity": 50,
                                    "price": 1000,
                                    "side": "BUY",
                                }
                            ],
                        }
                    ],
                    "asks": [],
                }
            },
            "pnl": {},
        }
        (states_dir / "state_000001.json").write_text(json.dumps(state))

        harness = CrossValidationHarness()
        result = harness._validate_test_output(test_dir)

        assert result.status == ValidationStatus.PASSED
        assert result.state_comparisons >= 1

    def test_validate_test_output_mismatch(self, tmp_path):
        """Should return FAILED when state doesn't match."""
        test_dir = tmp_path / "test_0"
        test_dir.mkdir()
        states_dir = test_dir / "states"
        states_dir.mkdir()

        # Write deltas.csv with one ADD
        deltas = """timestamp,sequence_num,delta_type,order_id,client_id,instrument_id,side,price,quantity,remaining_qty,trade_id,new_order_id,new_price,new_quantity
100,0,ADD,1,100,1,BUY,1000,50,50,0,0,0,0
"""
        (test_dir / "deltas.csv").write_text(deltas)
        (test_dir / "trades.csv").write_text(
            "timestamp,trade_id,instrument_id,buyer_id,seller_id,buyer_order_id,seller_order_id,price,quantity\n"
        )

        # Write mismatched state (wrong quantity)
        state = {
            "timestamp": 100,
            "sequence_num": 1,
            "order_books": {
                "1": {
                    "bids": [
                        {
                            "price": 1000,
                            "orders": [
                                {
                                    "order_id": 1,
                                    "client_id": 100,
                                    "quantity": 999,  # Wrong!
                                    "price": 1000,
                                    "side": "BUY",
                                }
                            ],
                        }
                    ],
                    "asks": [],
                }
            },
            "pnl": {},
        }
        (states_dir / "state_000001.json").write_text(json.dumps(state))

        harness = CrossValidationHarness()
        result = harness._validate_test_output(test_dir)

        assert result.status == ValidationStatus.FAILED
        assert result.state_failures >= 1
        assert any("quantity" in d for d in result.differences)
