"""
Database reader utilities for market-sim visualization and analysis tools.

Queries the PostgreSQL backend and returns data in the same format as the
CSV-based loaders, so visualizers and analyzers can use either source without
changes to their core logic.

All functions require psycopg2-binary:
    pip install psycopg2-binary
"""

from __future__ import annotations

import sys
from typing import Iterator

import numpy as np

try:
    import psycopg2
    import psycopg2.extras

    _PSYCOPG2_AVAILABLE = True
except ImportError:
    _PSYCOPG2_AVAILABLE = False


def _require_psycopg2() -> None:
    if not _PSYCOPG2_AVAILABLE:
        print(
            "Error: psycopg2-binary is required for DB mode. "
            "Install with: pip install psycopg2-binary",
            file=sys.stderr,
        )
        sys.exit(1)


# ---------------------------------------------------------------------------
# Order deltas
# ---------------------------------------------------------------------------


def iter_deltas(run_id: str, conn_str: str) -> Iterator[dict]:
    """
    Yield order delta rows for a run ordered by (timestamp, sequence_num).

    Yields string-keyed dicts matching the CSV DictReader format from
    deltas.csv.  NULL columns (trade_id, new_order_id, new_price,
    new_quantity) are coerced to "0" to match the CSV convention consumed
    by OrderBook.apply_delta().
    """
    _require_psycopg2()
    with psycopg2.connect(conn_str) as conn:
        with conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor) as cur:
            cur.execute(
                """
                SELECT
                    timestamp, sequence_num, delta_type, order_id, client_id,
                    instrument_id, side, price, quantity, remaining_qty,
                    COALESCE(trade_id,     0) AS trade_id,
                    COALESCE(new_order_id, 0) AS new_order_id,
                    COALESCE(new_price,    0) AS new_price,
                    COALESCE(new_quantity, 0) AS new_quantity
                FROM order_deltas
                WHERE run_id = %s
                ORDER BY timestamp, sequence_num
                """,
                (run_id,),
            )
            for row in cur:
                yield {k: str(v) for k, v in row.items()}


def load_all_deltas(run_id: str, conn_str: str) -> list[dict]:
    """Load all deltas for a run into a list (for in-memory navigation)."""
    return list(iter_deltas(run_id, conn_str))


# ---------------------------------------------------------------------------
# Market state
# ---------------------------------------------------------------------------


def load_market_state(
    run_id: str,
    conn_str: str,
    max_points: int | None = None,
) -> list[dict]:
    """
    Load market_state rows for a run ordered by timestamp.

    Returns a list of dicts with integer values for keys:
        timestamp, fair_price, best_bid, best_ask

    If max_points is set, the result is uniformly sampled down to that size.
    """
    _require_psycopg2()
    with psycopg2.connect(conn_str) as conn:
        with conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor) as cur:
            cur.execute(
                """
                SELECT timestamp, fair_price, best_bid, best_ask
                FROM market_state
                WHERE run_id = %s
                ORDER BY timestamp
                """,
                (run_id,),
            )
            rows = [dict(r) for r in cur]

    if max_points and len(rows) > max_points:
        indices = np.linspace(0, len(rows) - 1, max_points, dtype=int)
        rows = [rows[i] for i in indices]

    return rows


# ---------------------------------------------------------------------------
# Trades
# ---------------------------------------------------------------------------


def load_trades(run_id: str, conn_str: str) -> list[dict]:
    """
    Load all trades for a run ordered by timestamp.

    Returns a list of string-keyed dicts matching the CSV DictReader format
    from trades.csv.
    """
    _require_psycopg2()
    with psycopg2.connect(conn_str) as conn:
        with conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor) as cur:
            cur.execute(
                """
                SELECT timestamp, trade_id, instrument_id, buyer_id, seller_id,
                       buyer_order_id, seller_order_id, price, quantity,
                       aggressor_side, fair_price
                FROM trades
                WHERE run_id = %s
                ORDER BY timestamp
                """,
                (run_id,),
            )
            return [{k: str(v) for k, v in row.items()} for row in cur]


# ---------------------------------------------------------------------------
# Run metadata (agents)
# ---------------------------------------------------------------------------


def load_run_config(run_id: str, conn_str: str) -> dict:
    """
    Load the config JSONB blob stored for a run.

    psycopg2 deserialises JSONB automatically, so the returned dict has
    the same structure as metadata.json (including an "agents" list).

    Raises ValueError if the run_id is not found.
    """
    _require_psycopg2()
    with psycopg2.connect(conn_str) as conn:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT config FROM runs WHERE run_id = %s",
                (run_id,),
            )
            row = cur.fetchone()

    if row is None:
        raise ValueError(f"No run found with run_id={run_id}")

    return row[0]
