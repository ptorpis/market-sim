"""
Cross-validation tests for the adverse selection analyzer.

Runs C++ scenario tests that generate deterministic output, then validates
that the Python analyzer produces correct results from that output.

Each C++ scenario creates a known order flow with controlled fair prices.
The Python side reads the output CSVs and checks exact expected values.

Usage:
    pytest tests/test_adverse_selection.py -v
"""

import os
import subprocess
import tempfile
from pathlib import Path

import pytest

from tools.analyze_adverse_selection import (
    build_order_lifecycle,
    compute_mm_fills,
    load_agent_map,
    load_fair_price_series,
)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

MM_CLIENT_ID = 10
BUILD_DIR = Path("build/debug")
BINARY_NAME = "cross_validation_tests"


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def scenario_outputs():
    """
    Run the AdverseSelectionScenarioTest suite from cross_validation_tests
    and return list of test output dirs.

    Uses --gtest_filter to only run the AS scenarios, not the full
    cross-validation suite.
    """
    binary = BUILD_DIR / BINARY_NAME
    if not binary.exists():
        pytest.skip(f"C++ binary not found at {binary}. Build with cmake first.")

    with tempfile.TemporaryDirectory(prefix="as_test_") as tmpdir:
        env = os.environ.copy()
        env["AS_TEST_OUTPUT_DIR"] = tmpdir

        result = subprocess.run(
            [str(binary), "--gtest_filter=AdverseSelectionScenarioTest.*"],
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
        )

        if result.returncode != 0:
            pytest.fail(
                f"C++ test binary failed (rc={result.returncode}):\n"
                f"stdout: {result.stdout}\nstderr: {result.stderr}"
            )

        # Discover test directories in order
        test_dirs = sorted(
            [Path(tmpdir) / d for d in os.listdir(tmpdir) if d.startswith("test_")],
            key=lambda p: int(p.name.split("_")[1]),
        )

        if not test_dirs:
            pytest.fail("No test output directories found")

        yield test_dirs


def run_analyzer(test_dir, mm_client_id=MM_CLIENT_ID, horizons=None):
    """Run analyzer functions on a test directory, return list of MMFill."""
    if horizons is None:
        horizons = [100, 200]

    agent_map = load_agent_map(test_dir / "metadata.json")
    order_lifecycle = build_order_lifecycle(test_dir / "deltas.csv")
    ts_list, fp_list = load_fair_price_series(test_dir / "market_state.csv")
    return compute_mm_fills(
        test_dir / "trades.csv",
        mm_client_id,
        order_lifecycle,
        ts_list,
        fp_list,
        agent_map,
        horizons,
    )


# ---------------------------------------------------------------------------
# Test: Scenario 0 — BasicMMBuyFill
# ---------------------------------------------------------------------------


def test_basic_mm_buy_fill(scenario_outputs):
    """MM resting BUY is filled by NoiseTrader aggressor. Fair price 950, fill 1000."""
    test_dir = scenario_outputs[0]
    fills = run_analyzer(test_dir)

    assert len(fills) == 1, f"Expected 1 fill, got {len(fills)}"

    fill = fills[0]
    assert fill.mm_side == "BUY"
    assert fill.quote_age == 100  # fill at t=200, order added at t=100
    assert fill.fill_price == 1000
    assert fill.fair_price == 950
    assert fill.immediate_as == -50  # 950 - 1000
    assert fill.counterparty_id == 20
    assert fill.counterparty_type == "NoiseTrader"


# ---------------------------------------------------------------------------
# Test: Scenario 1 — BasicMMSellFill
# ---------------------------------------------------------------------------


def test_basic_mm_sell_fill(scenario_outputs):
    """MM resting SELL is filled by InformedTrader aggressor. Fair price 1050, fill 1000."""
    test_dir = scenario_outputs[1]
    fills = run_analyzer(test_dir)

    assert len(fills) == 1, f"Expected 1 fill, got {len(fills)}"

    fill = fills[0]
    assert fill.mm_side == "SELL"
    assert fill.quote_age == 100  # fill at t=200, order added at t=100
    assert fill.fill_price == 1000
    assert fill.fair_price == 1050
    assert fill.immediate_as == -50  # 1000 - 1050
    assert fill.counterparty_id == 30
    assert fill.counterparty_type == "InformedTrader"


# ---------------------------------------------------------------------------
# Test: Scenario 2 — ModifyResetsQuoteAge
# ---------------------------------------------------------------------------


def test_modify_resets_quote_age(scenario_outputs):
    """MODIFY at t=300 resets quote age clock. Fill at t=500 → age=200."""
    test_dir = scenario_outputs[2]
    fills = run_analyzer(test_dir)

    assert len(fills) == 1, f"Expected 1 fill, got {len(fills)}"

    fill = fills[0]
    assert fill.mm_side == "BUY"
    assert fill.quote_age == 200  # fill at t=500, MODIFY at t=300 (not ADD at t=100)
    assert fill.fill_price == 995
    assert fill.fair_price == 1000
    assert fill.immediate_as == 5  # 1000 - 995 (MM bought cheap relative to fair)


# ---------------------------------------------------------------------------
# Test: Scenario 3 — AggressorMMSkipped
# ---------------------------------------------------------------------------


def test_aggressor_mm_skipped(scenario_outputs):
    """MM is the aggressor (BUY hitting resting SELL) → 0 maker fills."""
    test_dir = scenario_outputs[3]
    fills = run_analyzer(test_dir)

    assert len(fills) == 0, f"Expected 0 fills (MM was aggressor), got {len(fills)}"


# ---------------------------------------------------------------------------
# Test: Scenario 4 — RealizedASWithChangingFairPrice
# ---------------------------------------------------------------------------


def test_realized_as_with_changing_fair_price(scenario_outputs):
    """Realized AS at multiple horizons with changing fair price schedule."""
    test_dir = scenario_outputs[4]
    horizons = [100, 200, 300]
    fills = run_analyzer(test_dir, horizons=horizons)

    assert len(fills) == 1, f"Expected 1 fill, got {len(fills)}"

    fill = fills[0]
    assert fill.mm_side == "BUY"
    assert fill.fill_price == 1000
    assert fill.fair_price == 950
    assert fill.immediate_as == -50  # 950 - 1000

    # Realized AS at horizon 100: fair_price at first ts >= 200+100=300 → 920
    # realized_as = 920 - 1000 = -80
    assert fill.realized_as[100] == -80, f"Expected -80, got {fill.realized_as[100]}"

    # Realized AS at horizon 200: fair_price at first ts >= 200+200=400 → 880
    # realized_as = 880 - 1000 = -120
    assert fill.realized_as[200] == -120, f"Expected -120, got {fill.realized_as[200]}"

    # Realized AS at horizon 300: fair_price at first ts >= 200+300=500 → 900
    # realized_as = 900 - 1000 = -100
    assert fill.realized_as[300] == -100, f"Expected -100, got {fill.realized_as[300]}"
