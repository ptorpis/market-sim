"""
Cross-validation runner for market simulator.

Orchestrates the full validation process:
1. Reads deltas.csv from C++ simulation
2. For each state file, replays deltas in Python
3. Compares Python state with C++ exported state
4. Reports any differences
"""

import json
import csv
from pathlib import Path
from typing import Iterator, Optional

from tools.visualizer.order_book import OrderBook
from tools.testing.state_comparator import StateComparator, ComparisonResult
from tools.testing.pnl_tracker import PnLTracker


class CrossValidator:
    """
    Validates Python replay against C++ state exports.

    The validator reads deltas.csv and state_*.json files produced by the
    C++ test harness, replays the deltas in Python, and compares state
    after each step.

    Usage:
        validator = CrossValidator(
            output_dir=Path("/tmp/test_output")
        )
        results = list(validator.validate_all())
        assert all(r.match for r in results)
    """

    def __init__(
        self,
        output_dir: Path,
        instrument_ids: Optional[list[int]] = None,
    ):
        """
        Args:
            output_dir: Directory containing deltas.csv, trades.csv, and states/
            instrument_ids: List of instrument IDs to validate (default: [1])
        """
        self.output_dir = Path(output_dir)
        self.deltas_file = self.output_dir / "deltas.csv"
        self.trades_file = self.output_dir / "trades.csv"
        self.states_dir = self.output_dir / "states"
        self.instrument_ids = instrument_ids or [1]
        self.comparator = StateComparator()

    def _load_cpp_state(self, seq_num: int) -> Optional[dict]:
        """Load C++ state export for a given sequence number."""
        state_file = self.states_dir / f"state_{seq_num:06d}.json"
        if not state_file.exists():
            return None

        with open(state_file, "r", encoding="utf-8") as f:
            return json.load(f)

    def _read_deltas(self) -> Iterator[dict]:
        """Read deltas from CSV file."""
        if not self.deltas_file.exists():
            return

        with open(self.deltas_file, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                yield row

    def _read_trades(self) -> Iterator[dict]:
        """Read trades from CSV file."""
        if not self.trades_file.exists():
            return

        with open(self.trades_file, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                yield row

    def validate_all(self) -> Iterator[ComparisonResult]:
        """
        Replay all deltas, comparing state after each step.

        The validator:
        1. Starts with empty order books
        2. For each state_*.json file (in sequence order):
           a. Applies all deltas up to the state's timestamp
           b. Compares Python state with C++ export
           c. Yields comparison result

        Note: State files use a "step counter" while deltas have their own sequence
        numbers. A single step can produce multiple deltas (e.g., a trade generates
        FILL deltas for both buyer and seller). We use timestamp for synchronization.

        Yields:
            ComparisonResult for each state comparison
        """
        # Initialize Python state
        books: dict[int, OrderBook] = {
            inst_id: OrderBook() for inst_id in self.instrument_ids
        }
        pnl_tracker = PnLTracker()

        # Collect all deltas sorted by (timestamp, sequence_num)
        all_deltas = list(self._read_deltas())
        all_deltas.sort(key=lambda d: (int(d["timestamp"]), int(d["sequence_num"])))

        # Collect all trades sorted by timestamp
        all_trades = list(self._read_trades())
        all_trades.sort(key=lambda t: int(t["timestamp"]))

        # Find all state files
        state_files = sorted(self.states_dir.glob("state_*.json"))
        if not state_files:
            yield ComparisonResult(
                match=False,
                sequence_num=-1,
                timestamp=-1,
                differences=["No state files found in states directory"],
            )
            return

        # Track which deltas and trades have been applied
        delta_idx = 0
        trade_idx = 0

        for state_file in state_files:
            # Extract sequence number from filename
            seq_num = int(state_file.stem.split("_")[1])

            # Load C++ state
            cpp_state = self._load_cpp_state(seq_num)
            if cpp_state is None:
                yield ComparisonResult(
                    match=False,
                    sequence_num=seq_num,
                    timestamp=-1,
                    differences=[f"Missing state file: {state_file}"],
                )
                continue

            cpp_timestamp = cpp_state.get("timestamp", -1)

            # Apply all deltas with timestamp <= cpp_timestamp
            while delta_idx < len(all_deltas):
                delta = all_deltas[delta_idx]
                if int(delta["timestamp"]) <= cpp_timestamp:
                    inst_id = int(delta.get("instrument_id", 1))
                    if inst_id in books:
                        books[inst_id].apply_delta(delta)
                    delta_idx += 1
                else:
                    break

            # Apply all trades with timestamp <= cpp_timestamp
            while trade_idx < len(all_trades):
                trade = all_trades[trade_idx]
                if int(trade["timestamp"]) <= cpp_timestamp:
                    pnl_tracker.on_trade(
                        buyer_id=int(trade["buyer_id"]),
                        seller_id=int(trade["seller_id"]),
                        price=int(trade["price"]),
                        quantity=int(trade["quantity"]),
                    )
                    trade_idx += 1
                else:
                    break

            # Compare states
            result = self.comparator.compare_full_state(
                cpp_state, books, pnl_tracker.get_state()
            )
            yield result

    def validate_final_state(self) -> ComparisonResult:
        """
        Validate only the final state (after all deltas applied).

        Useful for quick sanity checks.
        """
        results = list(self.validate_all())
        if not results:
            return ComparisonResult(
                match=False,
                sequence_num=-1,
                timestamp=-1,
                differences=["No states to validate"],
            )
        return results[-1]

    def run_and_report(self, verbose: bool = False) -> bool:
        """
        Run full validation and print report.

        Args:
            verbose: If True, print status for each comparison

        Returns:
            True if all comparisons pass
        """
        all_passed = True
        failures = []
        total = 0

        for result in self.validate_all():
            total += 1
            if verbose:
                print(result)

            if not result.match:
                all_passed = False
                failures.append(result)
                if not verbose:
                    # Print first few failures
                    if len(failures) <= 5:
                        print(f"FAIL: {result}")

        print(f"\n{'=' * 60}")
        print(f"Cross-validation complete: {'PASSED' if all_passed else 'FAILED'}")
        print(f"Total comparisons: {total}")
        print(f"Failures: {len(failures)}")

        if failures and not verbose:
            print(f"\nFirst failure details:")
            print(f"  Sequence: {failures[0].sequence_num}")
            print(f"  Timestamp: {failures[0].timestamp}")
            print(f"  Differences:")
            for diff in failures[0].differences[:10]:
                print(f"    - {diff}")

        return all_passed


def main():
    """CLI entry point for cross-validation."""
    import argparse
    import sys

    parser = argparse.ArgumentParser(
        description="Cross-validate Python replay against C++ simulation output"
    )
    parser.add_argument(
        "output_dir",
        type=Path,
        help="Directory containing deltas.csv, trades.csv, and states/",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Print status for each comparison"
    )
    parser.add_argument(
        "--instruments",
        type=int,
        nargs="+",
        default=[1],
        help="Instrument IDs to validate (default: 1)",
    )

    args = parser.parse_args()

    validator = CrossValidator(
        output_dir=args.output_dir,
        instrument_ids=args.instruments,
    )

    success = validator.run_and_report(verbose=args.verbose)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
