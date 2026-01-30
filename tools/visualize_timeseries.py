#!/usr/bin/env python3
"""
Time-series visualizer for market microstructure dynamics.

Plots spread, midpoint, and fair price evolution over time to analyze
price discovery and market efficiency. Uses simulation output files
(market_state.csv) for direct access to market state at each timestamp.

Usage:
    python visualize_timeseries.py output_dir                    # Plot all metrics
    python visualize_timeseries.py output_dir --metric spread    # Plot only spread
    python visualize_timeseries.py output_dir -o plot.png        # Save to file
    python visualize_timeseries.py output_dir --sample 1000      # Sample 1000 points
"""

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import matplotlib.pyplot as plt
import numpy as np


@dataclass
class MarketState:
    """A single point in the market state time series."""

    timestamp: int
    fair_price: int
    best_bid: int
    best_ask: int

    @property
    def midpoint(self) -> Optional[float]:
        """Calculate midpoint, or None if either side is missing."""
        if self.best_bid == 0 or self.best_ask == 0:
            return None
        return (self.best_bid + self.best_ask) / 2

    @property
    def spread(self) -> Optional[int]:
        """Calculate spread, or None if either side is missing."""
        if self.best_bid == 0 or self.best_ask == 0:
            return None
        return self.best_ask - self.best_bid


def load_market_state(
    market_state_path: Path,
    max_points: Optional[int] = None,
) -> list[MarketState]:
    """
    Load market state data from CSV file.

    Args:
        market_state_path: Path to market_state.csv
        max_points: If set, sample approximately this many points uniformly

    Returns:
        List of MarketState objects
    """
    states: list[MarketState] = []

    with open(market_state_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            states.append(
                MarketState(
                    timestamp=int(row["timestamp"]),
                    fair_price=int(row["fair_price"]),
                    best_bid=int(row["best_bid"]),
                    best_ask=int(row["best_ask"]),
                )
            )

    if not states:
        return []

    # Sample if requested
    if max_points and len(states) > max_points:
        indices = np.linspace(0, len(states) - 1, max_points, dtype=int)
        states = [states[i] for i in indices]

    return states


def plot_timeseries(
    states: list[MarketState],
    metrics: list[str],
    output_path: Optional[str] = None,
    title: Optional[str] = None,
) -> None:
    """
    Plot the requested market metrics over time.

    Args:
        states: List of MarketState from load_market_state
        metrics: List of metrics to plot: "mid", "spread", "fair", "bid", "ask"
        output_path: If provided, save plot to this path instead of displaying
        title: Optional custom title for the plot
    """
    if not states:
        print("No data points to plot.")
        return

    timestamps = [s.timestamp for s in states]

    # Determine subplot layout
    show_prices = any(m in metrics for m in ["mid", "fair", "bid", "ask"])
    show_spread = "spread" in metrics

    if show_prices and show_spread:
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), sharex=True)
        fig.subplots_adjust(hspace=0.1)
    elif show_prices:
        fig, ax1 = plt.subplots(figsize=(14, 5))
        ax2 = None
    elif show_spread:
        fig, ax2 = plt.subplots(figsize=(14, 4))
        ax1 = None
    else:
        print("No valid metrics specified.")
        return

    # Plot price metrics
    if ax1 is not None:
        # Check if we need dual y-axes (fair price diverges from market prices)
        show_market = any(m in metrics for m in ["mid", "bid", "ask"])
        show_fair = "fair" in metrics
        use_dual_axis = show_market and show_fair

        # Collect market price data
        mids = [s.midpoint for s in states]
        valid_mid_idx = [i for i, m in enumerate(mids) if m is not None]
        bids = [s.best_bid if s.best_bid != 0 else None for s in states]
        valid_bid_idx = [i for i, b in enumerate(bids) if b is not None]
        asks = [s.best_ask if s.best_ask != 0 else None for s in states]
        valid_ask_idx = [i for i, a in enumerate(asks) if a is not None]
        fair_prices = [s.fair_price for s in states]

        # Check if scales differ significantly (>5% divergence)
        if use_dual_axis and valid_mid_idx:
            market_mean = np.mean([mids[i] for i in valid_mid_idx])
            fair_mean = np.mean(fair_prices)
            scale_diff = abs(fair_mean - market_mean) / market_mean
            use_dual_axis = scale_diff > 0.05

        ax1_right = None
        if use_dual_axis:
            ax1_right = ax1.twinx()

        # Plot market prices on primary axis
        lines = []
        labels = []

        if "mid" in metrics and valid_mid_idx:
            (line,) = ax1.plot(
                [timestamps[i] for i in valid_mid_idx],
                [mids[i] for i in valid_mid_idx],
                label="Midpoint",
                color="blue",
                linewidth=0.8,
            )
            lines.append(line)
            labels.append("Midpoint")

        if "bid" in metrics and valid_bid_idx:
            (line,) = ax1.plot(
                [timestamps[i] for i in valid_bid_idx],
                [bids[i] for i in valid_bid_idx],
                label="Best Bid",
                color="green",
                linewidth=0.6,
                alpha=0.7,
            )
            lines.append(line)
            labels.append("Best Bid")

        if "ask" in metrics and valid_ask_idx:
            (line,) = ax1.plot(
                [timestamps[i] for i in valid_ask_idx],
                [asks[i] for i in valid_ask_idx],
                label="Best Ask",
                color="red",
                linewidth=0.6,
                alpha=0.7,
            )
            lines.append(line)
            labels.append("Best Ask")

        # Plot fair price (on secondary axis if scales differ)
        if "fair" in metrics:
            plot_ax = ax1_right if use_dual_axis else ax1
            (line,) = plot_ax.plot(
                timestamps,
                fair_prices,
                label="Fair Price",
                color="orange",
                linewidth=1.2,
                linestyle="--",
            )
            lines.append(line)
            labels.append("Fair Price")

            if use_dual_axis:
                ax1_right.set_ylabel("Fair Price", color="orange")
                ax1_right.tick_params(axis="y", labelcolor="orange")

        ax1.set_ylabel("Market Price (Bid/Ask/Mid)")
        ax1.legend(lines, labels, loc="upper left")
        ax1.grid(True, alpha=0.3)
        if title:
            ax1.set_title(title)
        else:
            ax1.set_title("Price Discovery: Midpoint vs Fair Price")

    # Plot spread
    if ax2 is not None:
        spreads = [s.spread for s in states]
        valid_idx = [i for i, sp in enumerate(spreads) if sp is not None]
        if valid_idx:
            ax2.fill_between(
                [timestamps[i] for i in valid_idx],
                [spreads[i] for i in valid_idx],
                alpha=0.3,
                color="purple",
            )
            ax2.plot(
                [timestamps[i] for i in valid_idx],
                [spreads[i] for i in valid_idx],
                label="Spread",
                color="purple",
                linewidth=0.8,
            )

        ax2.set_ylabel("Spread")
        ax2.set_xlabel("Timestamp")
        ax2.legend(loc="upper left")
        ax2.grid(True, alpha=0.3)
        if not show_prices:
            ax2.set_title("Bid-Ask Spread Over Time")

    plt.tight_layout()

    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches="tight")
        print(f"Saved plot to {output_path}")
    else:
        plt.show()


def plot_price_discovery_analysis(
    states: list[MarketState],
    output_path: Optional[str] = None,
) -> None:
    """
    Create a comprehensive price discovery analysis plot.

    Shows midpoint vs fair price, their difference, and spread in a 3-panel layout.
    """
    if not states:
        print("No data for price discovery analysis.")
        return

    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    fig.subplots_adjust(hspace=0.1)

    timestamps = [s.timestamp for s in states]
    fair_prices = [s.fair_price for s in states]

    # Panel 1: Midpoint vs Fair Price
    ax1 = axes[0]
    mids = [s.midpoint for s in states]
    valid_mid_idx = [i for i, m in enumerate(mids) if m is not None]

    if valid_mid_idx:
        ax1.plot(
            [timestamps[i] for i in valid_mid_idx],
            [mids[i] for i in valid_mid_idx],
            label="Midpoint",
            color="blue",
            linewidth=0.8,
        )

    ax1.plot(
        timestamps,
        fair_prices,
        label="Fair Price",
        color="orange",
        linewidth=1.2,
        linestyle="--",
    )

    ax1.set_ylabel("Price")
    ax1.legend(loc="upper left")
    ax1.grid(True, alpha=0.3)
    ax1.set_title("Price Discovery Analysis")

    # Panel 2: Pricing Error (Midpoint - Fair Price)
    ax2 = axes[1]

    errors = []
    error_times = []
    for i in valid_mid_idx:
        mid = mids[i]
        fp = fair_prices[i]
        errors.append(mid - fp)
        error_times.append(timestamps[i])

    if errors:
        ax2.axhline(y=0, color="black", linestyle="-", linewidth=0.5)
        ax2.fill_between(
            error_times,
            errors,
            alpha=0.3,
            color="green",
            where=[e >= 0 for e in errors],
        )
        ax2.fill_between(
            error_times,
            errors,
            alpha=0.3,
            color="red",
            where=[e < 0 for e in errors],
        )
        ax2.plot(error_times, errors, color="gray", linewidth=0.5)

        # Compute and display RMSE
        rmse = np.sqrt(np.mean(np.array(errors) ** 2))
        mae = np.mean(np.abs(errors))
        ax2.text(
            0.02,
            0.95,
            f"RMSE: {rmse:.2f}  MAE: {mae:.2f}",
            transform=ax2.transAxes,
            verticalalignment="top",
            fontsize=9,
            bbox=dict(boxstyle="round", facecolor="white", alpha=0.8),
        )

    ax2.set_ylabel("Pricing Error\n(Mid - Fair)")
    ax2.grid(True, alpha=0.3)

    # Panel 3: Spread
    ax3 = axes[2]
    spreads = [s.spread for s in states]
    valid_spread_idx = [i for i, sp in enumerate(spreads) if sp is not None]

    if valid_spread_idx:
        spread_times = [timestamps[i] for i in valid_spread_idx]
        spread_values = [spreads[i] for i in valid_spread_idx]
        ax3.fill_between(spread_times, spread_values, alpha=0.3, color="purple")
        ax3.plot(spread_times, spread_values, color="purple", linewidth=0.8)

        avg_spread = np.mean(spread_values)
        ax3.axhline(
            y=avg_spread,
            color="purple",
            linestyle="--",
            linewidth=1,
            alpha=0.7,
            label=f"Avg: {avg_spread:.1f}",
        )
        ax3.legend(loc="upper right")

    ax3.set_ylabel("Spread")
    ax3.set_xlabel("Timestamp")
    ax3.grid(True, alpha=0.3)

    plt.tight_layout()

    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches="tight")
        print(f"Saved plot to {output_path}")
    else:
        plt.show()


def main() -> None:
    """Entry point for the time-series visualizer CLI."""
    parser = argparse.ArgumentParser(
        description="Visualize market microstructure time series"
    )
    parser.add_argument(
        "output_dir",
        help="Path to simulation output directory (containing market_state.csv)",
    )
    parser.add_argument(
        "--metric",
        "-m",
        action="append",
        choices=["mid", "spread", "fair", "bid", "ask", "all"],
        help="Metrics to plot (can specify multiple, default: all)",
    )
    parser.add_argument(
        "--analysis",
        "-a",
        action="store_true",
        help="Show comprehensive price discovery analysis view",
    )
    parser.add_argument(
        "--output",
        "-o",
        metavar="PATH",
        help="Save plot to file instead of displaying",
    )
    parser.add_argument(
        "--sample",
        "-s",
        type=int,
        metavar="N",
        help="Sample approximately N points for faster plotting",
    )
    parser.add_argument(
        "--title",
        "-t",
        help="Custom title for the plot",
    )

    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    market_state_path = output_dir / "market_state.csv"

    if not market_state_path.exists():
        print(f"Error: {market_state_path} not found")
        return

    # Load market state data
    print("Loading market state data...")
    states = load_market_state(market_state_path, max_points=args.sample)
    print(f"  Loaded {len(states)} data points")

    if not states:
        print("No data points found.")
        return

    # Determine which metrics to plot
    metrics = args.metric or ["all"]
    if "all" in metrics:
        metrics = ["mid", "spread", "fair", "bid", "ask"]

    # Plot
    if args.analysis:
        plot_price_discovery_analysis(states, args.output)
    else:
        plot_timeseries(states, metrics, args.output, args.title)


if __name__ == "__main__":
    main()
