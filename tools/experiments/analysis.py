"""
Passive-edge metric, breakeven spread interpolation, and Roll spread estimator.

All functions operate on the CSV/JSON files produced by the C++ simulator.
Column names match those defined in include/persistence/records.hpp.
"""

from __future__ import annotations

import csv
from pathlib import Path
from typing import Optional

import numpy as np

# Must match the client_id assigned to the MarketMaker in config_builder.py.
MM_CLIENT_ID = 999


def read_mm_passive_edge(output_dir: Path, mm_client_id: int = MM_CLIENT_ID) -> float:
    """
    Compute the MM's average per-trade passive edge relative to fair price.

    For each trade where the MM is the passive (maker) side, computes:
      - MM sell (aggressor_side=BUY):  edge = price - fair_price
      - MM buy  (aggressor_side=SELL): edge = fair_price - price

    Positive edge means the MM received a better price than fair value.
    Negative edge means the MM was adversely selected (informed trader hit
    a stale quote that was worse than the current fair price).

    This metric cleanly separates spread income from adverse selection without
    the GBM drift contamination that plagues cash-only PnL. It is the direct
    empirical analog of the GM (1985) per-trade expected profit.

    Initial seeded orders (client_id 0) are excluded since they are not part
    of the MM's active quoting strategy.

    Args:
        output_dir: Directory containing trades.csv produced by the simulator.
        mm_client_id: The market maker's client_id as it appears in trades.csv.

    Returns:
        Mean per-trade passive edge in price units.

    Raises:
        ValueError: If no passive MM trades are found in trades.csv.
    """
    trades_path = output_dir / "trades.csv"
    edges: list[float] = []

    with open(trades_path, encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            buyer_id = int(row["buyer_id"])
            seller_id = int(row["seller_id"])
            aggressor_side = row["aggressor_side"]

            # Identify whether MM is the passive side and who the counterparty is.
            if buyer_id == mm_client_id and aggressor_side == "SELL":
                counterparty = seller_id
                mm_is_buyer = True
            elif seller_id == mm_client_id and aggressor_side == "BUY":
                counterparty = buyer_id
                mm_is_buyer = False
            else:
                continue  # MM is aggressor or not involved

            if counterparty == 0:
                continue  # skip initial seeded orders

            price = int(row["price"])
            fair_price = int(row["fair_price"])

            if mm_is_buyer:
                edges.append(fair_price - price)   # earn when fair > execution price
            else:
                edges.append(price - fair_price)   # earn when execution price > fair

    if not edges:
        raise ValueError(f"No passive MM trades found in {trades_path}")

    return float(np.mean(edges))


def find_breakeven_spread(pnl_by_half_spread: dict[int, float]) -> Optional[float]:
    """
    Find the half_spread where MM PnL = 0 by linear interpolation.

    Scans through half_spread values in ascending order to find the first
    pair that brackets a zero crossing, then interpolates linearly.

    Args:
        pnl_by_half_spread: Mapping from half_spread (int) to MM total PnL.

    Returns:
        Interpolated breakeven half_spread as a float, or None if the PnL
        never crosses zero within the given range.
    """
    items = sorted(pnl_by_half_spread.items())
    for (s0, p0), (s1, p1) in zip(items, items[1:]):
        if (p0 <= 0 <= p1) or (p1 <= 0 <= p0):
            if p1 == p0:
                return float(s0)
            t = -p0 / (p1 - p0)
            return s0 + t * (s1 - s0)
    return None


def roll_spread_estimator(trades_csv: Path) -> Optional[float]:
    """
    Roll (1984) spread estimator: spread = 2 * sqrt(-Cov(ΔP_t, ΔP_{t+1})).

    Computes the effective bid-ask spread implied by the serial correlation of
    consecutive trade price changes. A negative autocovariance indicates a
    bid-ask bounce (trades alternating between bid and ask), and its magnitude
    encodes the spread.

    Returns None if the autocovariance is non-negative (no bounce detected) or
    if there are fewer than 3 trades.

    Args:
        trades_csv: Path to trades.csv produced by the simulator.

    Returns:
        Estimated full bid-ask spread (2x half-spread) in price units, or None.
    """
    prices: list[float] = []
    with open(trades_csv, encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            prices.append(float(row["price"]))

    if len(prices) < 3:
        return None

    arr = np.array(prices)
    deltas = np.diff(arr)
    cov = float(np.cov(deltas[:-1], deltas[1:])[0, 1])

    if cov >= 0:
        return None
    return 2.0 * float(np.sqrt(-cov))
