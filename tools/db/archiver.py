"""
Archive completed simulation runs from the SSD PostgreSQL instance to
per-run parquet files on the HDD, then delete them from the SSD DB to
reclaim space.

Usage:
    # Dry run — show what would be archived without doing anything
    python -m tools.db.archiver --archive-dir /mnt/toshiba/market-sim/archive --dry-run

    # Archive all completed runs, keep them in the SSD DB afterward
    python -m tools.db.archiver --archive-dir /mnt/toshiba/market-sim/archive --no-delete

    # Archive and delete from SSD DB (default behaviour)
    python -m tools.db.archiver --archive-dir /mnt/toshiba/market-sim/archive

    # Use a non-default connection string
    python -m tools.db.archiver --archive-dir /mnt/toshiba/market-sim/archive \\
        --db-conn "postgresql://localhost:5434/market_sim?host=/tmp"

Output layout:
    <archive-dir>/
    ├── <run_id>/
    │   ├── run.json              # config + started_at from runs table
    │   ├── order_deltas.parquet
    │   ├── trades.parquet
    │   ├── pnl_snapshots.parquet
    │   └── market_state.parquet
    └── manifest.parquet          # one row per archived run (run_id, started_at, config)

Querying across archived runs with DuckDB:
    import duckdb
    con = duckdb.connect()
    df = con.execute(
        "SELECT * FROM read_parquet('/mnt/toshiba/market-sim/archive/*/trades.parquet')"
        " WHERE seller_id = 999"
    ).df()

Dependencies:
    pip install psycopg2-binary pyarrow pandas
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import pandas as pd
import psycopg2
import psycopg2.extras
import pyarrow as pa
import pyarrow.parquet as pq
from tqdm import tqdm

DB_CONN = "postgresql://localhost:5434/market_sim?host=/tmp"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _fetch_df(cur: psycopg2.extensions.cursor, query: str, params: tuple) -> pd.DataFrame:
    cur.execute(query, params)
    cols = [desc[0] for desc in cur.description]
    rows = cur.fetchall()
    return pd.DataFrame(rows, columns=cols)


def _load_manifest(archive_dir: Path) -> set[str]:
    """Return set of run_ids already present in the manifest."""
    manifest_path = archive_dir / "manifest.parquet"
    if not manifest_path.exists():
        return set()
    df = pd.read_parquet(manifest_path, columns=["run_id"])
    return set(df["run_id"].astype(str))


def _append_manifest(archive_dir: Path, run_id: str, started_at: object, config: dict) -> None:
    manifest_path = archive_dir / "manifest.parquet"
    new_row = pd.DataFrame([{
        "run_id": run_id,
        "started_at": str(started_at),
        "config": json.dumps(config),
    }])
    if manifest_path.exists():
        existing = pd.read_parquet(manifest_path)
        combined = pd.concat([existing, new_row], ignore_index=True)
    else:
        combined = new_row
    combined.to_parquet(manifest_path, index=False)


def _archive_run(
    run_id: str,
    started_at: object,
    config: dict,
    cur: psycopg2.extensions.cursor,
    run_dir: Path,
) -> None:
    """Export all tables for one run to parquet files in run_dir."""
    run_dir.mkdir(parents=True, exist_ok=True)

    # run.json
    (run_dir / "run.json").write_text(
        json.dumps({"run_id": run_id, "started_at": str(started_at), "config": config}, indent=2),
        encoding="utf-8",
    )

    tables = {
        "order_deltas": """
            SELECT timestamp, sequence_num, delta_type, order_id, client_id,
                   instrument_id, side, price, quantity, remaining_qty,
                   trade_id, new_order_id, new_price, new_quantity
            FROM order_deltas WHERE run_id = %s
            ORDER BY timestamp, sequence_num
        """,
        "trades": """
            SELECT timestamp, trade_id, instrument_id, buyer_id, seller_id,
                   buyer_order_id, seller_order_id, price, quantity,
                   aggressor_side, fair_price
            FROM trades WHERE run_id = %s
            ORDER BY timestamp
        """,
        "pnl_snapshots": """
            SELECT timestamp, client_id, long_position, short_position, cash, fair_price
            FROM pnl_snapshots WHERE run_id = %s
            ORDER BY timestamp, client_id
        """,
        "market_state": """
            SELECT timestamp, fair_price, best_bid, best_ask
            FROM market_state WHERE run_id = %s
            ORDER BY timestamp
        """,
    }

    for table, query in tables.items():
        df = _fetch_df(cur, query, (run_id,))
        df.to_parquet(run_dir / f"{table}.parquet", index=False)


def _delete_run(run_id: str, conn: psycopg2.extensions.connection) -> None:
    """Delete all rows for a run from the SSD DB (child tables first)."""
    with conn.cursor() as cur:
        for table in ("order_deltas", "trades", "pnl_snapshots", "market_state"):
            cur.execute(f"DELETE FROM {table} WHERE run_id = %s", (run_id,))  # noqa: S608
        cur.execute("DELETE FROM runs WHERE run_id = %s", (run_id,))
    conn.commit()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def archive(
    archive_dir: Path,
    conn_str: str,
    *,
    delete_after: bool = True,
    dry_run: bool = False,
    verbose: bool = True,
) -> int:
    """
    Archive all completed runs from the SSD DB to parquet on HDD.

    Returns the number of runs archived.
    """
    archive_dir.mkdir(parents=True, exist_ok=True)
    already_archived = _load_manifest(archive_dir)

    with psycopg2.connect(conn_str) as conn:
        with conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor) as cur:
            cur.execute(
                """
                SELECT r.run_id, r.started_at, r.config
                FROM runs r
                WHERE EXISTS (SELECT 1 FROM trades t WHERE t.run_id = r.run_id)
                ORDER BY r.started_at
                """
            )
            candidates = [dict(row) for row in cur]

    to_archive = [r for r in candidates if str(r["run_id"]) not in already_archived]

    if verbose:
        print(f"Runs in DB:        {len(candidates)}")
        print(f"Already archived:  {len(already_archived)}")
        print(f"To archive:        {len(to_archive)}")
        if delete_after:
            print("Mode:              archive + delete from SSD DB")
        else:
            print("Mode:              archive only (keeping in SSD DB)")

    if dry_run:
        print("\n[dry-run] No files written.")
        return 0

    if not to_archive:
        print("Nothing to do.")
        return 0

    archived = 0
    with psycopg2.connect(conn_str) as conn:
        with conn.cursor() as cur:
            for row in tqdm(to_archive, desc="Archiving", unit="run", disable=not verbose):
                run_id = str(row["run_id"])
                run_dir = archive_dir / run_id
                _archive_run(run_id, row["started_at"], row["config"], cur, run_dir)
                _append_manifest(archive_dir, run_id, row["started_at"], row["config"])
                if delete_after:
                    _delete_run(run_id, conn)
                archived += 1

    if verbose:
        print(f"\nDone. {archived} run(s) archived to {archive_dir}")
        if delete_after:
            print("SSD DB has been cleaned up.")

    return archived


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Archive simulation runs from SSD PostgreSQL to HDD parquet files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--archive-dir",
        required=True,
        metavar="DIR",
        help="Root directory for parquet output (e.g. /mnt/toshiba/market-sim/archive)",
    )
    parser.add_argument(
        "--db-conn",
        default=DB_CONN,
        metavar="DSN",
        help=f"PostgreSQL connection string (default: {DB_CONN})",
    )
    parser.add_argument(
        "--no-delete",
        action="store_true",
        help="Archive to parquet but keep runs in the SSD DB (default: delete after archiving)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be archived without writing any files",
    )
    args = parser.parse_args()

    try:
        archive(
            Path(args.archive_dir),
            args.db_conn,
            delete_after=not args.no_delete,
            dry_run=args.dry_run,
        )
    except psycopg2.OperationalError as exc:
        print(f"Error: could not connect to PostgreSQL — {exc}", file=sys.stderr)
        print(f"  Connection string: {args.db_conn}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
