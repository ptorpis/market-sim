"""
Phase 2 GM replication experiment.

Runs a parameter sweep over (sigma, pi, half_spread) to find the market
maker's break-even spread as a function of the informed trader ratio and
asset volatility. Verifies the core GM (1985) prediction:

    equilibrium_spread ∝ pi * sigma

For each (sigma, pi) pair, we sweep half_spread values, average MM PnL
across replicates, and interpolate the zero-crossing to get the empirical
equilibrium spread.

Usage:
    python -m tools.experiments.gm_replication
    python -m tools.experiments.gm_replication --dry-run
    python -m tools.experiments.gm_replication --results-dir /tmp/gm --n-workers 8
    python -m tools.experiments.gm_replication --results-dir /tmp/gm  # resume

Dependencies: pandas, matplotlib, numpy (pip install pandas if missing)
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from .analysis import find_breakeven_spread, roll_spread_estimator
from .config_builder import build_gm_config
from .sweep import run_sweep

# ---------------------------------------------------------------------------
# Sweep specification
#
# Sigma values: 0.1%, 0.2%, 0.5% per price tick. With initial_price=1_000_000
# and tick_size=1_000, a 1-sigma GBM step is:
#   sigma * 1_000_000 price units per sqrt(tick) → per-step std dev of
#   sigma=0.001 → 1000 units, sigma=0.002 → 2000, sigma=0.005 → 5000.
#
# Metric: average per-trade passive edge (price - fair_price for MM sells,
# fair_price - price for MM buys). Breakeven is where this crosses zero.
# Unlike cash PnL, this is independent of GBM drift and inventory imbalance.
#
# MM update_interval=20 ticks; GBM steps every tick_size=1000 ticks. The
# stale-quote window is ~2% of each GBM period. This shrinks adverse selection
# from stale quotes enough that the breakeven falls within the sweep range for
# all (sigma, pi) combinations. Estimated breakevens:
#   sigma=0.005, pi=0.5 → ~80–120 price units
#   sigma=0.001, pi=0.1 → ~5–15 price units
# The sweep [5, 10, 20, 40, 80, 150, 300] covers the full range.
#
# Note: the same seed_base is used for all half_spread values within a given
# (sigma, pi, replicate), so the fair price path and agent arrival patterns
# are identical across the half_spread sweep — a controlled comparison.
# ---------------------------------------------------------------------------

SIGMA_VALUES = [0.001, 0.002, 0.005]
PI_VALUES = [0.10, 0.20, 0.30, 0.40, 0.50]
HALF_SPREAD_VALUES = [5, 10, 20, 40, 80, 150, 300]
N_REPLICATES = 3
N_TOTAL_TRADERS = 20
DURATION = 500_000
PNL_SNAPSHOT_INTERVAL = 1_000


def build_param_grid(seed_offset: int = 0) -> list[dict]:
    """
    Build the full parameter grid including replicates.

    Each entry is a dict of kwargs passed directly to build_gm_config.
    Replicates share the same (sigma, pi, half_spread) but differ in seed_base,
    giving independent draws of the fair price path for variance estimation.
    """
    grid = []
    for r_idx in range(N_REPLICATES):
        for s_idx, sigma in enumerate(SIGMA_VALUES):
            for p_idx, pi in enumerate(PI_VALUES):
                # Unique seed per (replicate, sigma, pi); same across half_spread
                # so the underlying dynamics are held fixed for the spread sweep.
                seed_base = seed_offset + r_idx * 10_000 + s_idx * 100 + p_idx
                for half_spread in HALF_SPREAD_VALUES:
                    grid.append(
                        {
                            "sigma": sigma,
                            "pi": pi,
                            "half_spread": half_spread,
                            "n_total_traders": N_TOTAL_TRADERS,
                            "duration": DURATION,
                            "pnl_snapshot_interval": PNL_SNAPSHOT_INTERVAL,
                            "seed_base": seed_base,
                            "observation_noise": 0.0,
                            "inventory_skew_factor": 0.0,
                        }
                    )
    return grid


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------


def compute_breakevens(results: pd.DataFrame) -> pd.DataFrame:
    """
    For each (sigma, pi), average MM passive edge across replicates per half_spread,
    then interpolate the breakeven spread.

    Returns a DataFrame with columns: sigma, pi, breakeven_half_spread.
    Rows where no zero crossing was found have breakeven_half_spread = NaN.
    """
    avg = (
        results.groupby(["sigma", "pi", "half_spread"], as_index=False)["mm_avg_passive_edge"]
        .mean()
    )

    rows = []
    for (sigma, pi), group in avg.groupby(["sigma", "pi"]):
        pnl_by_spread = dict(zip(group["half_spread"], group["mm_avg_passive_edge"]))
        be = find_breakeven_spread(pnl_by_spread)
        rows.append({"sigma": sigma, "pi": pi, "breakeven_half_spread": be})

    return pd.DataFrame(rows)


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------


def plot_breakeven_vs_pi(breakevens: pd.DataFrame, plots_dir: Path) -> None:
    """Plot equilibrium spread vs pi, one line per sigma value."""
    plots_dir.mkdir(parents=True, exist_ok=True)
    fig, ax = plt.subplots(figsize=(8, 5))

    for sigma, group in breakevens.groupby("sigma"):
        group = group.sort_values("pi").dropna(subset=["breakeven_half_spread"])
        if group.empty:
            continue
        ax.plot(group["pi"], group["breakeven_half_spread"], marker="o", label=f"σ={sigma}")

    ax.set_xlabel("π (informed trader fraction)")
    ax.set_ylabel("Equilibrium half-spread (price units)")
    ax.set_title("GM Replication: Equilibrium Spread vs π")
    ax.legend()
    ax.grid(True, alpha=0.3)

    path = plots_dir / "eq_spread_vs_pi.png"
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"Saved {path}")


def plot_breakeven_vs_sigma(breakevens: pd.DataFrame, plots_dir: Path) -> None:
    """Plot equilibrium spread vs sigma, one line per pi value."""
    plots_dir.mkdir(parents=True, exist_ok=True)
    fig, ax = plt.subplots(figsize=(8, 5))

    for pi, group in breakevens.groupby("pi"):
        group = group.sort_values("sigma").dropna(subset=["breakeven_half_spread"])
        if group.empty:
            continue
        ax.plot(group["sigma"], group["breakeven_half_spread"], marker="o", label=f"π={pi:.2f}")

    ax.set_xlabel("σ (GBM volatility)")
    ax.set_ylabel("Equilibrium half-spread (price units)")
    ax.set_title("GM Replication: Equilibrium Spread vs σ")
    ax.legend()
    ax.grid(True, alpha=0.3)

    path = plots_dir / "eq_spread_vs_sigma.png"
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"Saved {path}")


def plot_pnl_curves(results: pd.DataFrame, plots_dir: Path) -> None:
    """
    Plot MM avg passive edge vs half_spread for each (sigma, pi) pair.

    Useful for visually confirming that the break-even interpolation is
    sensible and that the edge curve changes sign within the sweep range.
    One figure per sigma value, one line per pi.
    """
    plots_dir.mkdir(parents=True, exist_ok=True)
    avg = (
        results.groupby(["sigma", "pi", "half_spread"], as_index=False)["mm_avg_passive_edge"]
        .mean()
    )

    for sigma, sigma_group in avg.groupby("sigma"):
        fig, ax = plt.subplots(figsize=(8, 5))
        for pi, pi_group in sigma_group.groupby("pi"):
            pi_group = pi_group.sort_values("half_spread")
            ax.plot(
                pi_group["half_spread"],
                pi_group["mm_avg_passive_edge"],
                marker="o",
                label=f"π={pi:.2f}",
            )
        ax.axhline(y=0, color="black", linestyle="--", linewidth=0.8)
        ax.set_xlabel("Half-spread (price units)")
        ax.set_ylabel("MM avg passive edge (price units)")
        ax.set_title(f"MM Passive Edge vs Half-Spread (σ={sigma})")
        ax.legend()
        ax.grid(True, alpha=0.3)

        sigma_tag = f"{sigma:.4f}".replace(".", "_")
        path = plots_dir / f"pnl_curves_sigma{sigma_tag}.png"
        fig.savefig(path, dpi=150, bbox_inches="tight")
        plt.close(fig)
        print(f"Saved {path}")


# ---------------------------------------------------------------------------
# Roll estimator sanity check
# ---------------------------------------------------------------------------


def print_roll_estimates(results: pd.DataFrame) -> None:
    """
    For one representative run per (sigma, pi), compute and print the Roll
    (1984) spread estimate from trades.csv.

    Provides a model-free sanity check: the Roll estimate should be in the
    same ballpark as the interpolated equilibrium spread.
    """
    print("\nRoll (1984) Spread Estimates (2 × half-spread, first replicate per (σ, π)):")
    print(f"  {'sigma':>8} {'pi':>6} {'roll_spread':>12}")
    print("  " + "-" * 30)

    seen: set = set()
    for _, row in results.iterrows():
        key = (row["sigma"], row["pi"])
        if key in seen or not row["output_dir"]:
            continue
        seen.add(key)
        trades_csv = Path(row["output_dir"]) / "trades.csv"
        if not trades_csv.exists():
            continue
        est = roll_spread_estimator(trades_csv)
        est_str = f"{est:.1f}" if est is not None else "N/A"
        print(f"  {row['sigma']:>8.3f} {row['pi']:>6.2f} {est_str:>12}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "GM replication: sweep (σ, π, half_spread) and find the market "
            "maker's equilibrium spread. Verifies spread ∝ π·σ."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--binary",
        default="./build/debug/MarketSimulator",
        help="Path to compiled MarketSimulator binary (default: ./build/debug/MarketSimulator)",
    )
    parser.add_argument(
        "--results-dir",
        default="./experiment_results/gm_replication",
        help="Parent directory for simulation outputs and summary CSVs",
    )
    parser.add_argument(
        "--n-workers",
        type=int,
        default=4,
        help="Number of parallel simulation threads (default: 4)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print parameter grid and a sample config without running anything",
    )
    parser.add_argument(
        "--no-skip",
        action="store_true",
        help="Re-run even if output already exists (default: skip completed runs)",
    )
    parser.add_argument(
        "--seed-offset",
        type=int,
        default=0,
        help="Added to all seed_base values (useful for independent replication sets)",
    )
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    binary = Path(args.binary)
    param_grid = build_param_grid(seed_offset=args.seed_offset)

    if args.dry_run:
        print(f"Parameter grid: {len(param_grid)} runs")
        print(f"  sigma:        {SIGMA_VALUES}")
        print(f"  pi:           {PI_VALUES}")
        print(f"  half_spread:  {HALF_SPREAD_VALUES}")
        print(f"  replicates:   {N_REPLICATES}")
        print(f"  n_traders:    {N_TOTAL_TRADERS}")
        print(f"  duration:     {DURATION:,} ticks")
        print(f"\nSample config (first run):")
        print(json.dumps(build_gm_config(**param_grid[0]), indent=2))
        return

    if not binary.exists():
        print(f"Error: binary not found at {binary}. Build the project first:")
        print("  cmake -S . -B build/debug && cmake --build build/debug --parallel 4")
        sys.exit(1)

    print(
        f"Running {len(param_grid)} simulations in {results_dir}"
        f" with {args.n_workers} workers..."
    )
    results = run_sweep(
        param_grid,
        results_dir,
        binary,
        n_workers=args.n_workers,
        skip_existing=not args.no_skip,
        verbose=True,
    )

    # Save raw results for external analysis / reproducibility.
    results_csv = results_dir / "results.csv"
    results.to_csv(results_csv, index=False)
    print(f"\nSaved raw results to {results_csv}")

    # Compute and save breakeven spreads.
    breakevens = compute_breakevens(results)
    breakeven_csv = results_dir / "breakeven.csv"
    breakevens.to_csv(breakeven_csv, index=False)
    print(f"Saved breakeven results to {breakeven_csv}")

    print("\nBreakeven half-spreads:")
    print(breakevens.to_string(index=False))

    # Plots.
    plots_dir = results_dir / "plots"
    plot_breakeven_vs_pi(breakevens, plots_dir)
    plot_breakeven_vs_sigma(breakevens, plots_dir)
    plot_pnl_curves(results, plots_dir)

    # Roll estimator sanity check.
    print_roll_estimates(results)


if __name__ == "__main__":
    main()
