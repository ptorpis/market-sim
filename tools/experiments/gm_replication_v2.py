"""
GM replication v2 — higher resolution and more replicates.

Addresses the two most actionable caveats from the preliminary results:

  1. σ=0.005 resolution: the original [150, 300] bracket contained all three
     σ=0.005 breakevens (178–250) with only two anchor points. This sweep adds
     175, 200, and 225 between 150 and 300 so the interpolation has five points
     in that range instead of two.

  2. Replicate count: 3 replicates is sufficient for monotonicity checks but
     produces wide confidence intervals at high σ / low π. This sweep uses 10
     replicates per (σ, π) cell.

Everything else (σ values, π values, duration, n_traders, metric) is held
fixed relative to v1 so the results are directly comparable.

Usage:
    python -m tools.experiments.gm_replication_v2
    python -m tools.experiments.gm_replication_v2 --dry-run
    python -m tools.experiments.gm_replication_v2 --n-workers 8 --db-conn "..."
    python -m tools.experiments.gm_replication_v2 --results-dir /tmp/gm_v2  # resume
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
# ---------------------------------------------------------------------------

SIGMA_VALUES = [0.001, 0.002, 0.005]
PI_VALUES = [0.10, 0.20, 0.30, 0.40, 0.50]

# Extended grid: three extra points (175, 200, 225) tighten the σ=0.005
# interpolation; the rest are unchanged from v1.
HALF_SPREAD_VALUES = [5, 10, 20, 40, 80, 150, 175, 200, 225, 300]

# 10 replicates → narrower CIs, especially for high-σ low-π cells.
N_REPLICATES = 10

N_TOTAL_TRADERS = 20
DURATION = 500_000
PNL_SNAPSHOT_INTERVAL = 1_000

DB_CONN = "postgresql://localhost:5433/market_sim?host=/tmp"


def build_param_grid(seed_offset: int = 0) -> list[dict]:
    """
    Build the full parameter grid including replicates.

    Seed assignment mirrors v1 so the first 3 replicates use identical seeds,
    making individual runs directly comparable to the original sweep.
    """
    grid = []
    for r_idx in range(N_REPLICATES):
        for s_idx, sigma in enumerate(SIGMA_VALUES):
            for p_idx, pi in enumerate(PI_VALUES):
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
# Analysis  (identical to v1)
# ---------------------------------------------------------------------------


def compute_breakevens(results: pd.DataFrame) -> pd.DataFrame:
    avg = (
        results.groupby(["sigma", "pi", "half_spread"], as_index=False)[
            "mm_avg_passive_edge"
        ].mean()
    )

    rows = []
    for (sigma, pi), group in avg.groupby(["sigma", "pi"]):
        pnl_by_spread = dict(zip(group["half_spread"], group["mm_avg_passive_edge"]))
        be = find_breakeven_spread(pnl_by_spread)
        rows.append({"sigma": sigma, "pi": pi, "breakeven_half_spread": be})

    return pd.DataFrame(rows)


def compute_ci(results: pd.DataFrame) -> pd.DataFrame:
    """
    Compute per-(σ, π, half_spread) mean and 95% CI across replicates.

    Used for the PnL curve plots to show uncertainty bands.
    """
    def _ci95(x: pd.Series) -> float:
        return 1.96 * x.std(ddof=1) / (len(x) ** 0.5) if len(x) > 1 else float("nan")

    agg = results.groupby(["sigma", "pi", "half_spread"])["mm_avg_passive_edge"].agg(
        mean="mean", ci95=_ci95
    ).reset_index()
    return agg


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------


def plot_breakeven_vs_pi(breakevens: pd.DataFrame, plots_dir: Path) -> None:
    plots_dir.mkdir(parents=True, exist_ok=True)
    fig, ax = plt.subplots(figsize=(8, 5))

    for sigma, group in breakevens.groupby("sigma"):
        group = group.sort_values("pi").dropna(subset=["breakeven_half_spread"])
        if group.empty:
            continue
        ax.plot(group["pi"], group["breakeven_half_spread"], marker="o", label=f"σ={sigma}")

    ax.set_xlabel("π (informed trader fraction)")
    ax.set_ylabel("Equilibrium half-spread (price units)")
    ax.set_title("GM Replication v2: Equilibrium Spread vs π")
    ax.legend()
    ax.grid(True, alpha=0.3)

    path = plots_dir / "eq_spread_vs_pi.png"
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"Saved {path}")


def plot_breakeven_vs_sigma(breakevens: pd.DataFrame, plots_dir: Path) -> None:
    plots_dir.mkdir(parents=True, exist_ok=True)
    fig, ax = plt.subplots(figsize=(8, 5))

    for pi, group in breakevens.groupby("pi"):
        group = group.sort_values("sigma").dropna(subset=["breakeven_half_spread"])
        if group.empty:
            continue
        ax.plot(group["sigma"], group["breakeven_half_spread"], marker="o", label=f"π={pi:.2f}")

    ax.set_xlabel("σ (GBM volatility)")
    ax.set_ylabel("Equilibrium half-spread (price units)")
    ax.set_title("GM Replication v2: Equilibrium Spread vs σ")
    ax.legend()
    ax.grid(True, alpha=0.3)

    path = plots_dir / "eq_spread_vs_sigma.png"
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"Saved {path}")


def plot_pnl_curves(results: pd.DataFrame, plots_dir: Path) -> None:
    """
    Plot MM avg passive edge vs half_spread for each (sigma, pi) pair,
    with 95% CI bands from the 10 replicates.
    """
    plots_dir.mkdir(parents=True, exist_ok=True)
    agg = compute_ci(results)

    for sigma, sigma_group in agg.groupby("sigma"):
        fig, ax = plt.subplots(figsize=(8, 5))
        for pi, pi_group in sigma_group.groupby("pi"):
            pi_group = pi_group.sort_values("half_spread")
            xs = pi_group["half_spread"].values
            ys = pi_group["mean"].values
            ci = pi_group["ci95"].values
            (line,) = ax.plot(xs, ys, marker="o", label=f"π={pi:.2f}")
            ax.fill_between(xs, ys - ci, ys + ci, alpha=0.15, color=line.get_color())

        ax.axhline(y=0, color="black", linestyle="--", linewidth=0.8)
        ax.set_xlabel("Half-spread (price units)")
        ax.set_ylabel("MM avg passive edge (price units)")
        ax.set_title(f"MM Passive Edge vs Half-Spread (σ={sigma}) — 95% CI bands")
        ax.legend()
        ax.grid(True, alpha=0.3)

        sigma_tag = f"{sigma:.4f}".replace(".", "_")
        path = plots_dir / f"pnl_curves_sigma{sigma_tag}.png"
        fig.savefig(path, dpi=150, bbox_inches="tight")
        plt.close(fig)
        print(f"Saved {path}")


def print_roll_estimates(results: pd.DataFrame) -> None:
    print("\nRoll (1984) Spread Estimates (first replicate per (σ, π)):")
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
            "GM replication v2: higher-resolution half_spread grid and 10 replicates."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--binary",
        default="./build/release/MarketSimulator",
        help="Path to compiled MarketSimulator binary",
    )
    parser.add_argument(
        "--results-dir",
        required=True,
        help="Parent directory for run_id.txt files and summary CSVs/plots",
    )
    parser.add_argument(
        "--n-workers",
        type=int,
        default=6,
        help="Number of parallel simulation threads (default: 6)",
    )
    parser.add_argument(
        "--db-conn",
        default=DB_CONN,
        metavar="DSN",
        help=f"PostgreSQL connection string (default: {DB_CONN})",
    )
    parser.add_argument(
        "--no-db",
        action="store_true",
        help="Disable PostgreSQL persistence (CSV only)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print parameter grid and a sample config without running anything",
    )
    parser.add_argument(
        "--no-skip",
        action="store_true",
        help="Re-run even if output already exists",
    )
    parser.add_argument(
        "--seed-offset",
        type=int,
        default=0,
        help="Added to all seed_base values",
    )
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    binary = Path(args.binary)
    param_grid = build_param_grid(seed_offset=args.seed_offset)
    db_conn = None if args.no_db else args.db_conn

    if args.dry_run:
        print(f"Parameter grid: {len(param_grid)} runs")
        print(f"  sigma:        {SIGMA_VALUES}")
        print(f"  pi:           {PI_VALUES}")
        print(f"  half_spread:  {HALF_SPREAD_VALUES}")
        print(f"  replicates:   {N_REPLICATES}")
        print(f"  n_traders:    {N_TOTAL_TRADERS}")
        print(f"  duration:     {DURATION:,} ticks")
        print(f"  db_conn:      {db_conn or '(disabled)'}")
        print(f"\nSample config (first run):")
        print(json.dumps(build_gm_config(**param_grid[0]), indent=2))
        return

    if not binary.exists():
        print(f"Error: binary not found at {binary}")
        sys.exit(1)

    print(
        f"Running {len(param_grid)} simulations in {results_dir}"
        f" with {args.n_workers} workers"
        + (f" → DB {db_conn}" if db_conn else " (CSV only)")
        + "..."
    )
    results = run_sweep(
        param_grid,
        results_dir,
        binary,
        n_workers=args.n_workers,
        skip_existing=not args.no_skip,
        verbose=True,
        db_connection_string=db_conn,
    )

    results_csv = results_dir / "results.csv"
    results.to_csv(results_csv, index=False)
    print(f"\nSaved raw results to {results_csv}")

    breakevens = compute_breakevens(results)
    breakeven_csv = results_dir / "breakeven.csv"
    breakevens.to_csv(breakeven_csv, index=False)
    print(f"Saved breakeven results to {breakeven_csv}")

    print("\nBreakeven half-spreads:")
    pivot = breakevens.pivot(index="sigma", columns="pi", values="breakeven_half_spread")
    print(pivot.to_string())

    plots_dir = results_dir / "plots"
    plot_breakeven_vs_pi(breakevens, plots_dir)
    plot_breakeven_vs_sigma(breakevens, plots_dir)
    plot_pnl_curves(results, plots_dir)

    print_roll_estimates(results)


if __name__ == "__main__":
    main()
