"""
Pytest fixtures for cross-validation testing.

Provides fixtures for:
- Temporary test directories
- C++ test harness execution
- State file generation
"""

import tempfile


import subprocess
from pathlib import Path

import shutil

import pytest


@pytest.fixture
def temp_test_dir():
    """Create a temporary directory for test output."""
    test_dir = tempfile.mkdtemp(prefix="cross_val_test_")
    yield Path(test_dir)
    shutil.rmtree(test_dir, ignore_errors=True)


@pytest.fixture
def cpp_test_binary():
    """
    Get path to the C++ cross_validation_tests binary.

    Assumes it's built in the default build directory.
    Override with --cpp-binary pytest option if needed.
    """
    # Check common build locations
    project_root = Path(__file__).parent.parent.parent
    possible_paths = [
        project_root / "build" / "debug" / "cross_validation_tests",
        project_root / "build" / "cross_validation_tests",
        project_root / "cmake-build-debug" / "cross_validation_tests",
        project_root / "cmake-build-release" / "cross_validation_tests",
    ]

    for path in possible_paths:
        if path.exists():
            return path

    pytest.skip("C++ cross_validation_tests binary not found. Build with cmake first.")


@pytest.fixture
def run_cpp_scenario(cpp_test_binary, temp_test_dir):
    """
    Factory fixture to run a specific C++ test scenario.

    Returns a function that takes a scenario name and runs the C++ test,
    returning the output directory path.
    """

    def _run_scenario(scenario_name: str) -> Path:
        """
        Run a C++ test scenario and return the output directory.

        Args:
            scenario_name: Name of the GTest to run (e.g., "BasicOperations")

        Returns:
            Path to the output directory containing deltas.csv, trades.csv, states/
        """
        # Create scenario-specific output directory
        output_dir = temp_test_dir / scenario_name
        output_dir.mkdir(parents=True, exist_ok=True)

        # Run the specific test
        result = subprocess.run(
            [
                str(cpp_test_binary),
                f"--gtest_filter=*{scenario_name}*",
                f"--output_dir={output_dir}",
            ],
            capture_output=True,
            text=True,
            cwd=temp_test_dir,
            env={"OUTPUT_DIR": str(output_dir)},
            check=False,
        )

        if result.returncode != 0:
            pytest.fail(
                f"C++ test {scenario_name} failed:\n"
                f"stdout: {result.stdout}\n"
                f"stderr: {result.stderr}"
            )

        return output_dir

    return _run_scenario


@pytest.fixture
def sample_state_json():
    """Sample C++ state JSON for testing comparator directly."""
    return {
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
                                "timestamp": 100,
                                "instrument_id": 1,
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


@pytest.fixture
def sample_state_with_trade():
    """Sample C++ state JSON with P&L from a trade."""
    return {
        "timestamp": 200,
        "sequence_num": 3,
        "order_books": {
            "1": {
                "bids": [],
                "asks": [],
            }
        },
        "pnl": {
            "100": {"long_position": 50, "short_position": 0, "cash": -50000},
            "101": {"long_position": 0, "short_position": 50, "cash": 50000},
        },
    }


@pytest.fixture
def sample_deltas_content():
    """Sample deltas.csv content for testing."""
    return """timestamp,sequence_num,delta_type,order_id,client_id,instrument_id,side,price,quantity,remaining_qty,trade_id,new_order_id,new_price,new_quantity
100,0,ADD,1,100,1,BUY,1000,50,50,0,0,0,0
200,1,ADD,2,101,1,SELL,1000,50,50,0,0,0,0
200,2,FILL,1,100,1,BUY,1000,50,0,1,0,0,0
200,3,FILL,2,101,1,SELL,1000,50,0,1,0,0,0
"""


@pytest.fixture
def sample_trades_content():
    """Sample trades.csv content for testing."""
    return """timestamp,trade_id,instrument_id,buyer_id,seller_id,buyer_order_id,seller_order_id,price,quantity
200,1,1,100,101,1,2,1000,50
"""
