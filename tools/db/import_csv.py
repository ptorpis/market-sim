"""
Backfill historical CSV run directories into the PostgreSQL database.

Reads metadata.json to construct the runs row, then bulk-inserts each CSV
via COPY (psycopg2 copy_expert) for maximum throughput.

Usage:
    python tools/db/import_csv.py \\
        --run-dir experiment_results/gm_replication/run_00000 \\
        --conn "postgresql://localhost:5433/market_sim?host=/tmp"
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import uuid
from io import StringIO
from pathlib import Path

try:
    import psycopg2
except ImportError:
    print("Error: psycopg2-binary is required. Install with: pip install psycopg2-binary",
          file=sys.stderr)
    sys.exit(1)


def _csv_to_stringio(path: Path, transform=None) -> StringIO:
    """Read a CSV (with header) and return a tab-delimited StringIO for COPY."""
    buf = StringIO()
    with open(path, encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if transform is not None:
                row = transform(row)
            buf.write("\t".join(row.values()) + "\n")
    buf.seek(0)
    return buf


def _null_if_zero(row: dict, fields: list[str]) -> dict:
    """Replace '0' with '\\N' (psycopg2 COPY null) for nullable fields."""
    for field in fields:
        if row.get(field) == "0":
            row[field] = "\\N"
    return row


def import_run(run_dir: Path, conn_str: str) -> str:
    """
    Import a single run directory into the database.

    Returns:
        The run_id UUID assigned to this run.
    """
    run_dir = Path(run_dir)
    metadata_path = run_dir / "metadata.json"
    if not metadata_path.exists():
        raise FileNotFoundError(f"metadata.json not found in {run_dir}")

    with open(metadata_path, encoding="utf-8") as f:
        metadata = json.load(f)

    # Reuse existing run_id if a previous import wrote one
    run_id_path = run_dir / "run_id.txt"
    if run_id_path.exists():
        run_id = run_id_path.read_text(encoding="utf-8").strip()
    else:
        run_id = str(uuid.uuid4())

    conn = psycopg2.connect(conn_str)
    try:
        with conn:
            with conn.cursor() as cur:
                cur.execute(
                    "INSERT INTO runs (run_id, config) VALUES (%s::uuid, %s::jsonb)"
                    " ON CONFLICT (run_id) DO UPDATE SET config = EXCLUDED.config",
                    (run_id, json.dumps(metadata)),
                )

            nullable_delta_fields = ["trade_id", "new_order_id", "new_price",
                                     "new_quantity"]

            for csv_file, table, columns, transform in [
                (
                    run_dir / "deltas.csv",
                    "order_deltas",
                    ["run_id", "timestamp", "sequence_num", "delta_type", "order_id",
                     "client_id", "instrument_id", "side", "price", "quantity",
                     "remaining_qty", "trade_id", "new_order_id", "new_price",
                     "new_quantity"],
                    lambda row: {"run_id": run_id,
                                 **_null_if_zero(row, nullable_delta_fields)},
                ),
                (
                    run_dir / "trades.csv",
                    "trades",
                    ["run_id", "timestamp", "trade_id", "instrument_id", "buyer_id",
                     "seller_id", "buyer_order_id", "seller_order_id", "price",
                     "quantity", "aggressor_side", "fair_price"],
                    lambda row: {"run_id": run_id, **row},
                ),
                (
                    run_dir / "pnl.csv",
                    "pnl_snapshots",
                    ["run_id", "timestamp", "client_id", "long_position",
                     "short_position", "cash", "fair_price"],
                    lambda row: {"run_id": run_id, **row},
                ),
                (
                    run_dir / "market_state.csv",
                    "market_state",
                    ["run_id", "timestamp", "fair_price", "best_bid", "best_ask"],
                    lambda row: {"run_id": run_id, **row},
                ),
            ]:
                if not csv_file.exists():
                    print(f"  Warning: {csv_file.name} not found, skipping.",
                          file=sys.stderr)
                    continue

                buf = _csv_to_stringio(csv_file, transform)
                col_list = ", ".join(columns)
                with conn.cursor() as cur:
                    cur.copy_expert(
                        f"COPY {table} ({col_list}) FROM STDIN WITH (FORMAT text,"
                        f" DELIMITER E'\\t', NULL '\\N')",
                        buf,
                    )
    finally:
        conn.close()

    run_id_path.write_text(run_id + "\n", encoding="utf-8")
    return run_id


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run-dir", required=True,
                        help="Path to a single run output directory")
    parser.add_argument("--conn", required=True,
                        help="PostgreSQL connection string")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    run_id = import_run(Path(args.run_dir), args.conn)
    if args.verbose:
        print(f"Imported {args.run_dir} as run_id={run_id}")
    else:
        print(run_id)


if __name__ == "__main__":
    main()
