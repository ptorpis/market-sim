#!/usr/bin/env python3
"""
Adverse selection analyzer for market maker resting quotes.

Measures how the age of a market maker's resting quote relates to the
adverse selection experienced when that quote is filled. Quantifies the
stale-quote hypothesis: older quotes suffer worse adverse selection because
they haven't been updated to reflect the latest fair price.

Usage:
    python -m tools.analyze_adverse_selection output/                    # Auto-detect MM
    python -m tools.analyze_adverse_selection output/ --mm-id 200       # Specific MM
    python -m tools.analyze_adverse_selection output/ --plot             # Show plots
    python -m tools.analyze_adverse_selection output/ --horizons 50 100 # Custom horizons
"""

import argparse
import bisect
import csv
import json
import statistics
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import matplotlib.pyplot as plt
import numpy as np


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------


@dataclass
class MMFill:
    """A single fill where the market maker was the resting (maker) side."""

    fill_timestamp: int
    trade_id: int
    mm_order_id: int
    mm_side: str  # "BUY" or "SELL"
    quote_age: int
    fill_price: int
    fair_price: int
    immediate_as: int
    realized_as: dict[int, Optional[int]]  # horizon -> value or None
    counterparty_id: int
    counterparty_type: str


@dataclass
class AgentInfo:
    """Agent metadata from metadata.json."""

    client_id: int
    agent_type: str


@dataclass
class BucketStats:
    """Summary statistics for a quote age bucket."""

    label: str
    count: int
    mean_immediate_as: float
    median_immediate_as: float
    mean_realized_as: dict[int, Optional[float]]
    informed_pct: float


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------


def load_agent_map(metadata_path: Path) -> dict[int, AgentInfo]:
    """Load agent client_id -> AgentInfo mapping from metadata.json."""
    with open(metadata_path, "r", encoding="utf-8") as f:
        meta = json.load(f)

    agent_map: dict[int, AgentInfo] = {}
    for agent in meta["agents"]:
        cid = int(agent["client_id"])
        agent_map[cid] = AgentInfo(client_id=cid, agent_type=agent["type"])
    return agent_map


def find_market_makers(agent_map: dict[int, AgentInfo]) -> list[int]:
    """Return client_ids of all MarketMaker agents."""
    return [
        info.client_id
        for info in agent_map.values()
        if info.agent_type == "MarketMaker"
    ]


def build_order_lifecycle(deltas_path: Path) -> dict[int, int]:
    """
    Scan deltas.csv to build order_id -> most recent ADD/MODIFY timestamp.

    MODIFY resets the clock because the MM actively updated the quote.
    A MODIFY with a price change creates a new order (new_order_id), so we
    also register the new_order_id with the MODIFY timestamp.
    """
    lifecycle: dict[int, int] = {}
    with open(deltas_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            delta_type = row["delta_type"]
            if delta_type == "ADD":
                order_id = int(row["order_id"])
                timestamp = int(row["timestamp"])
                lifecycle[order_id] = timestamp
            elif delta_type == "MODIFY":
                timestamp = int(row["timestamp"])
                order_id = int(row["order_id"])
                lifecycle[order_id] = timestamp
                # Price-change MODIFYs create a replacement order with new_order_id
                new_order_id = int(row["new_order_id"])
                if new_order_id != 0:
                    lifecycle[new_order_id] = timestamp
    return lifecycle


def load_fair_price_series(market_state_path: Path) -> tuple[list[int], list[int]]:
    """
    Load sorted (timestamp, fair_price) arrays from market_state.csv.

    Returns:
        (timestamps, fair_prices) - parallel sorted lists for bisect lookups.
    """
    timestamps: list[int] = []
    fair_prices: list[int] = []
    with open(market_state_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            timestamps.append(int(row["timestamp"]))
            fair_prices.append(int(row["fair_price"]))
    return timestamps, fair_prices


def lookup_fair_price_at(
    ts_list: list[int],
    fp_list: list[int],
    target_ts: int,
) -> Optional[int]:
    """Find fair price at first timestamp >= target_ts via binary search."""
    idx = bisect.bisect_left(ts_list, target_ts)
    if idx < len(ts_list):
        return fp_list[idx]
    return None


# ---------------------------------------------------------------------------
# Core computation
# ---------------------------------------------------------------------------


def compute_mm_fills(
    trades_path: Path,
    mm_client_id: int,
    order_lifecycle: dict[int, int],
    ts_list: list[int],
    fp_list: list[int],
    agent_map: dict[int, AgentInfo],
    horizons: list[int],
) -> list[MMFill]:
    """
    Process trades.csv to extract fills where the MM was the maker (resting side).

    For each qualifying fill, compute quote age, immediate adverse selection,
    and realized adverse selection at each horizon.
    """
    fills: list[MMFill] = []

    with open(trades_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            aggressor_side = row["aggressor_side"]
            buyer_id = int(row["buyer_id"])
            seller_id = int(row["seller_id"])
            buyer_order_id = int(row["buyer_order_id"])
            seller_order_id = int(row["seller_order_id"])

            # Determine if MM was the resting (maker) side
            if aggressor_side == "BUY" and seller_id == mm_client_id:
                mm_order_id = seller_order_id
                mm_side = "SELL"
                counterparty_id = buyer_id
            elif aggressor_side == "SELL" and buyer_id == mm_client_id:
                mm_order_id = buyer_order_id
                mm_side = "BUY"
                counterparty_id = seller_id
            else:
                continue

            fill_timestamp = int(row["timestamp"])
            fill_price = int(row["price"])
            fair_price = int(row["fair_price"])
            trade_id = int(row["trade_id"])

            # Quote age
            birth_ts = order_lifecycle.get(mm_order_id)
            if birth_ts is None:
                # Order not found in deltas — skip
                continue
            quote_age = fill_timestamp - birth_ts

            # Immediate adverse selection
            if mm_side == "BUY":
                immediate_as = fair_price - fill_price
            else:
                immediate_as = fill_price - fair_price

            # Realized adverse selection at each horizon
            realized_as: dict[int, Optional[int]] = {}
            for h in horizons:
                future_fp = lookup_fair_price_at(ts_list, fp_list, fill_timestamp + h)
                if future_fp is not None:
                    if mm_side == "BUY":
                        realized_as[h] = future_fp - fill_price
                    else:
                        realized_as[h] = fill_price - future_fp
                else:
                    realized_as[h] = None

            # Counterparty type
            cp_info = agent_map.get(counterparty_id)
            counterparty_type = cp_info.agent_type if cp_info else "Unknown"

            fills.append(
                MMFill(
                    fill_timestamp=fill_timestamp,
                    trade_id=trade_id,
                    mm_order_id=mm_order_id,
                    mm_side=mm_side,
                    quote_age=quote_age,
                    fill_price=fill_price,
                    fair_price=fair_price,
                    immediate_as=immediate_as,
                    realized_as=realized_as,
                    counterparty_id=counterparty_id,
                    counterparty_type=counterparty_type,
                )
            )

    return fills


# ---------------------------------------------------------------------------
# Summary statistics
# ---------------------------------------------------------------------------


def compute_bucket_boundaries(fills: list[MMFill], num_buckets: int) -> list[int]:
    """Compute quartile-based bucket boundaries from observed quote ages."""
    ages = sorted(f.quote_age for f in fills)
    if not ages:
        return []
    boundaries: list[int] = []
    for i in range(1, num_buckets):
        pct = i / num_buckets
        idx = int(pct * len(ages))
        idx = min(idx, len(ages) - 1)
        boundaries.append(ages[idx])
    return boundaries


def assign_bucket(quote_age: int, boundaries: list[int]) -> int:
    """Return the bucket index for a given quote age."""
    return bisect.bisect_right(boundaries, quote_age)


def bucket_label(idx: int, boundaries: list[int]) -> str:
    """Human-readable label for a bucket index."""
    if not boundaries:
        return "[0, inf)"
    if idx == 0:
        return f"[0, {boundaries[0]})"
    elif idx < len(boundaries):
        return f"[{boundaries[idx - 1]}, {boundaries[idx]})"
    else:
        return f"[{boundaries[-1]}, inf)"


def compute_summary(
    fills: list[MMFill],
    horizons: list[int],
    num_buckets: int,
) -> tuple[list[int], list[BucketStats]]:
    """
    Compute per-bucket summary statistics.

    Returns:
        (boundaries, bucket_stats_list)
    """
    boundaries = compute_bucket_boundaries(fills, num_buckets)

    # Group fills by bucket
    buckets: dict[int, list[MMFill]] = {}
    for f in fills:
        b = assign_bucket(f.quote_age, boundaries)
        buckets.setdefault(b, []).append(f)

    stats_list: list[BucketStats] = []
    for b_idx in range(num_buckets):
        b_fills = buckets.get(b_idx, [])
        label = bucket_label(b_idx, boundaries)

        if not b_fills:
            stats_list.append(
                BucketStats(
                    label=label,
                    count=0,
                    mean_immediate_as=0.0,
                    median_immediate_as=0.0,
                    mean_realized_as={h: None for h in horizons},
                    informed_pct=0.0,
                )
            )
            continue

        imm_as_values = [f.immediate_as for f in b_fills]
        mean_realized: dict[int, Optional[float]] = {}
        for h in horizons:
            h_values = [f.realized_as[h] for f in b_fills if f.realized_as[h] is not None]
            mean_realized[h] = statistics.mean(h_values) if h_values else None

        informed_count = sum(
            1 for f in b_fills if f.counterparty_type == "InformedTrader"
        )

        stats_list.append(
            BucketStats(
                label=label,
                count=len(b_fills),
                mean_immediate_as=statistics.mean(imm_as_values),
                median_immediate_as=statistics.median(imm_as_values),
                mean_realized_as=mean_realized,
                informed_pct=(informed_count / len(b_fills)) * 100,
            )
        )

    return boundaries, stats_list


# ---------------------------------------------------------------------------
# Console output
# ---------------------------------------------------------------------------


def print_summary(
    fills: list[MMFill],
    mm_client_id: int,
    horizons: list[int],
    num_buckets: int,
) -> None:
    """Print a summary table to the console."""
    if not fills:
        print("No MM maker fills found.")
        return

    # Counterparty breakdown
    cp_counts: dict[str, int] = {}
    for f in fills:
        cp_counts[f.counterparty_type] = cp_counts.get(f.counterparty_type, 0) + 1

    print(f"\nAdverse Selection Analysis (MM client_id={mm_client_id})")
    print("=" * 60)
    print(f"Total MM fills: {len(fills)} (maker only)")
    for cp_type, count in sorted(cp_counts.items()):
        pct = (count / len(fills)) * 100
        print(f"  vs {cp_type}: {count} ({pct:.1f}%)")

    # Bucket summary
    _, bucket_stats = compute_summary(fills, horizons, num_buckets)

    # Pick a representative horizon for the table
    display_horizon = horizons[len(horizons) // 2] if horizons else None

    print(f"\nBy Quote Age:")
    header = f"  {'Bucket':<14} | {'Count':>5} | {'Mean Imm. AS':>12} | {'Med Imm. AS':>11}"
    if display_horizon is not None:
        header += f" | {'Mean AS@' + str(display_horizon):>12}"
    header += f" | {'% Informed':>10}"
    print(header)
    print("  " + "-" * (len(header) - 2))

    for bs in bucket_stats:
        line = f"  {bs.label:<14} | {bs.count:>5} | {bs.mean_immediate_as:>12.1f} | {bs.median_immediate_as:>11.1f}"
        if display_horizon is not None:
            ras = bs.mean_realized_as.get(display_horizon)
            ras_str = f"{ras:.1f}" if ras is not None else "N/A"
            line += f" | {ras_str:>12}"
        line += f" | {bs.informed_pct:>9.1f}%"
        print(line)


# ---------------------------------------------------------------------------
# CSV output
# ---------------------------------------------------------------------------


def write_per_fill_csv(
    fills: list[MMFill],
    horizons: list[int],
    csv_path: Path,
) -> None:
    """Write per-fill adverse selection data to CSV."""
    fieldnames = [
        "fill_timestamp",
        "trade_id",
        "mm_order_id",
        "mm_side",
        "quote_age",
        "fill_price",
        "fair_price",
        "immediate_as",
    ]
    for h in horizons:
        fieldnames.append(f"realized_as_{h}")
    fieldnames.extend(["counterparty_id", "counterparty_type"])

    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        for fill in fills:
            row = {
                "fill_timestamp": fill.fill_timestamp,
                "trade_id": fill.trade_id,
                "mm_order_id": fill.mm_order_id,
                "mm_side": fill.mm_side,
                "quote_age": fill.quote_age,
                "fill_price": fill.fill_price,
                "fair_price": fill.fair_price,
                "immediate_as": fill.immediate_as,
            }
            for h in horizons:
                val = fill.realized_as[h]
                row[f"realized_as_{h}"] = val if val is not None else ""
            row["counterparty_id"] = fill.counterparty_id
            row["counterparty_type"] = fill.counterparty_type
            writer.writerow(row)

    print(f"Wrote {len(fills)} fills to {csv_path}")


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------


def plot_adverse_selection(
    fills: list[MMFill],
    horizons: list[int],
    num_buckets: int,
    mm_client_id: int,
    output_path: Optional[str] = None,
) -> None:
    """
    Create adverse selection plots:
    1. Scatter of immediate AS vs quote age, colored by counterparty type.
    2. Realized AS at multiple horizons vs quote age (binned line plot).
    """
    if not fills:
        print("No data to plot.")
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))
    fig.suptitle(
        f"Adverse Selection Analysis (MM client_id={mm_client_id})",
        fontsize=13,
    )

    # --- Panel 1: Scatter with binned means ---
    type_colors = {
        "InformedTrader": "red",
        "NoiseTrader": "blue",
        "Unknown": "gray",
    }

    # Plot scatter by counterparty type
    by_type: dict[str, tuple[list[int], list[int]]] = {}
    for f in fills:
        by_type.setdefault(f.counterparty_type, ([], []))
        by_type[f.counterparty_type][0].append(f.quote_age)
        by_type[f.counterparty_type][1].append(f.immediate_as)

    for cp_type, (ages, as_vals) in sorted(by_type.items()):
        color = type_colors.get(cp_type, "gray")
        ax1.scatter(
            ages,
            as_vals,
            alpha=0.3,
            s=10,
            color=color,
            label=cp_type,
        )

    # Binned means overlay
    boundaries, bucket_stats = compute_summary(fills, horizons, num_buckets)
    bin_centers = []
    bin_means = []
    for i, bs in enumerate(bucket_stats):
        if bs.count == 0:
            continue
        # Compute bin center from fills in this bucket
        b_fills = [f for f in fills if assign_bucket(f.quote_age, boundaries) == i]
        center = statistics.mean(f.quote_age for f in b_fills)
        bin_centers.append(center)
        bin_means.append(bs.mean_immediate_as)

    if bin_centers:
        ax1.plot(
            bin_centers,
            bin_means,
            color="black",
            linewidth=2,
            marker="o",
            markersize=6,
            label="Binned Mean",
            zorder=5,
        )

    ax1.axhline(y=0, color="gray", linestyle="--", linewidth=0.5)
    ax1.set_xlabel("Quote Age (ticks)")
    ax1.set_ylabel("Immediate Adverse Selection")
    ax1.set_title("Immediate AS vs Quote Age")
    ax1.legend(loc="upper left", fontsize=8)
    ax1.grid(True, alpha=0.3)

    # --- Panel 2: Realized AS at multiple horizons ---
    for h in horizons:
        h_centers = []
        h_means = []
        for i, bs in enumerate(bucket_stats):
            if bs.count == 0:
                continue
            ras = bs.mean_realized_as.get(h)
            if ras is None:
                continue
            b_fills = [f for f in fills if assign_bucket(f.quote_age, boundaries) == i]
            center = statistics.mean(f.quote_age for f in b_fills)
            h_centers.append(center)
            h_means.append(ras)

        if h_centers:
            ax2.plot(
                h_centers,
                h_means,
                marker="o",
                markersize=5,
                linewidth=1.5,
                label=f"horizon={h}",
            )

    ax2.axhline(y=0, color="gray", linestyle="--", linewidth=0.5)
    ax2.set_xlabel("Quote Age (ticks)")
    ax2.set_ylabel("Realized Adverse Selection")
    ax2.set_title("Realized AS at Multiple Horizons")
    ax2.legend(loc="upper left", fontsize=8)
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()

    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches="tight")
        print(f"Saved plot to {output_path}")
    else:
        plt.show()


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def main() -> None:
    """Entry point for the adverse selection analyzer CLI."""
    parser = argparse.ArgumentParser(
        description="Analyze adverse selection on market maker resting quotes"
    )
    parser.add_argument(
        "output_dir",
        help="Directory containing trades.csv, deltas.csv, market_state.csv, metadata.json",
    )
    parser.add_argument(
        "--mm-id",
        type=int,
        metavar="ID",
        help="Market maker client_id (auto-detected if only one MM exists)",
    )
    parser.add_argument(
        "--horizons",
        type=int,
        nargs="+",
        default=[50, 100, 200, 500],
        metavar="H",
        help="Realized AS horizons in ticks (default: 50 100 200 500)",
    )
    parser.add_argument(
        "--buckets",
        type=int,
        default=4,
        metavar="N",
        help="Number of quote age buckets (default: 4, quartile-based)",
    )
    parser.add_argument(
        "--plot",
        action="store_true",
        help="Show plots",
    )
    parser.add_argument(
        "--output",
        metavar="PATH",
        help="Save plot to file",
    )
    parser.add_argument(
        "--csv",
        metavar="PATH",
        help="Write per-fill CSV (default: OUTPUT_DIR/adverse_selection.csv)",
    )
    parser.add_argument(
        "--no-csv",
        action="store_true",
        help="Skip writing per-fill CSV",
    )

    args = parser.parse_args()

    output_dir = Path(args.output_dir)

    # Validate required files
    required_files = {
        "trades.csv": output_dir / "trades.csv",
        "deltas.csv": output_dir / "deltas.csv",
        "market_state.csv": output_dir / "market_state.csv",
        "metadata.json": output_dir / "metadata.json",
    }
    for name, path in required_files.items():
        if not path.exists():
            print(f"Error: {path} not found")
            return

    # Load agent metadata
    print("Loading metadata...")
    agent_map = load_agent_map(required_files["metadata.json"])

    # Resolve MM client_id
    if args.mm_id is not None:
        mm_client_id = args.mm_id
        if mm_client_id not in agent_map:
            print(f"Warning: client_id {mm_client_id} not found in metadata.json")
    else:
        mm_ids = find_market_makers(agent_map)
        if len(mm_ids) == 1:
            mm_client_id = mm_ids[0]
            print(f"  Auto-detected MarketMaker client_id={mm_client_id}")
        elif len(mm_ids) == 0:
            print("Error: No MarketMaker agents found in metadata.json")
            return
        else:
            print(
                f"Error: Multiple MarketMaker agents found: {mm_ids}. "
                "Use --mm-id to specify one."
            )
            return

    # Build order lifecycle map
    print("Building order lifecycle map from deltas.csv...")
    order_lifecycle = build_order_lifecycle(required_files["deltas.csv"])
    print(f"  Tracked {len(order_lifecycle)} orders")

    # Load fair price series for horizon lookups
    print("Loading fair price series from market_state.csv...")
    ts_list, fp_list = load_fair_price_series(required_files["market_state.csv"])
    print(f"  Loaded {len(ts_list)} market state snapshots")

    # Compute MM fills
    print("Computing MM fills from trades.csv...")
    fills = compute_mm_fills(
        required_files["trades.csv"],
        mm_client_id,
        order_lifecycle,
        ts_list,
        fp_list,
        agent_map,
        args.horizons,
    )
    print(f"  Found {len(fills)} MM maker fills")

    if not fills:
        print("No maker fills found for this market maker.")
        return

    # Console summary
    print_summary(fills, mm_client_id, args.horizons, args.buckets)

    # Write per-fill CSV
    if not args.no_csv:
        csv_path = Path(args.csv) if args.csv else output_dir / "adverse_selection.csv"
        write_per_fill_csv(fills, args.horizons, csv_path)

    # Plots
    if args.plot or args.output:
        plot_adverse_selection(
            fills, args.horizons, args.buckets, mm_client_id, args.output
        )


if __name__ == "__main__":
    main()
