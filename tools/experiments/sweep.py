"""
Parameter grid construction and parallel batch execution.

Uses ThreadPoolExecutor so parallel runs are I/O-bound (each thread manages
a subprocess), which avoids pickling overhead and works cleanly with the
relative imports in this package.
"""

from __future__ import annotations

import itertools
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import pandas as pd
from tqdm import tqdm

from .analysis import read_mm_passive_edge, read_mm_passive_edge_db, MM_CLIENT_ID
from .config_builder import build_gm_config
from .runner import read_run_id, run_simulation


def parameter_grid(**param_lists: list) -> list[dict]:
    """
    Produce a list of dicts, one per combination of parameter values.

    Example:
        parameter_grid(sigma=[0.001, 0.005], pi=[0.2, 0.3])
        -> [{"sigma": 0.001, "pi": 0.2},
            {"sigma": 0.001, "pi": 0.3},
            {"sigma": 0.005, "pi": 0.2},
            {"sigma": 0.005, "pi": 0.3}]
    """
    keys = list(param_lists.keys())
    values = list(param_lists.values())
    return [dict(zip(keys, combo)) for combo in itertools.product(*values)]


def _run_one(
    params: dict,
    run_dir: Path,
    binary: Path,
    timeout: int,
    skip_existing: bool,
    db_connection_string: str | None,
) -> dict:
    """
    Execute or recover one parameter combination.

    In CSV mode: skip if trades.csv exists; read edge from CSV.
    In DB mode: skip if run_id.txt exists; read edge from DB.
    """
    if db_connection_string is not None:
        run_id_path = run_dir / "run_id.txt"
        if skip_existing and run_id_path.exists():
            run_id = read_run_id(run_dir)
            edge = read_mm_passive_edge_db(run_id, db_connection_string, MM_CLIENT_ID)
            return {**params, "mm_avg_passive_edge": edge, "output_dir": "",
                    "run_id": run_id}
        config = build_gm_config(**params)
        run_simulation(config, run_dir, binary, timeout=timeout,
                       db_connection_string=db_connection_string)
        run_id = read_run_id(run_dir)
        edge = read_mm_passive_edge_db(run_id, db_connection_string, MM_CLIENT_ID)
        return {**params, "mm_avg_passive_edge": edge, "output_dir": "",
                "run_id": run_id}

    trades_path = run_dir / "trades.csv"
    if skip_existing and trades_path.exists():
        edge = read_mm_passive_edge(run_dir, MM_CLIENT_ID)
        run_id = read_run_id(run_dir)
        return {**params, "mm_avg_passive_edge": edge, "output_dir": str(run_dir),
                "run_id": run_id}

    config = build_gm_config(**params)
    run_simulation(config, run_dir, binary, timeout=timeout,
                   db_connection_string=db_connection_string)
    edge = read_mm_passive_edge(run_dir, MM_CLIENT_ID)
    run_id = read_run_id(run_dir)
    return {**params, "mm_avg_passive_edge": edge, "output_dir": str(run_dir),
            "run_id": run_id}


def run_sweep(
    param_grid: list[dict],
    results_dir: Path,
    binary: Path = Path("./build/debug/MarketSimulator"),
    *,
    n_workers: int = 4,
    timeout: int = 300,
    skip_existing: bool = True,
    verbose: bool = True,
    db_connection_string: str | None = None,
) -> pd.DataFrame:
    """
    Run a parameter grid of simulations and return aggregated results.

    Each entry in param_grid must be a dict of kwargs accepted by
    build_gm_config. Each run's output goes to results_dir/run_XXXXX/.

    When skip_existing=True (the default), runs whose run_dir/pnl.csv already
    exists are not re-run, making the sweep safely resumable after interruption.

    Args:
        param_grid: List of kwargs dicts for build_gm_config.
        results_dir: Parent directory; each run gets a numbered subdirectory.
        binary: Path to the compiled MarketSimulator binary.
        n_workers: Number of parallel simulation threads.
        timeout: Per-run timeout in seconds.
        skip_existing: Skip runs whose output already exists.
        verbose: Print progress as runs complete.

    Returns:
        DataFrame with one row per run; columns = all params + mm_avg_passive_edge
        + output_dir + run_id (when DB backend is active). Rows are in completion
        order (not submission order). Failed runs have mm_avg_passive_edge=NaN.
    """
    results_dir.mkdir(parents=True, exist_ok=True)

    futures: dict = {}
    with ThreadPoolExecutor(max_workers=n_workers) as pool:
        for i, params in enumerate(param_grid):
            run_dir = results_dir / f"run_{i:05d}"
            future = pool.submit(_run_one, params, run_dir, binary, timeout,
                                 skip_existing, db_connection_string)
            futures[future] = (i, params)

        rows = []
        pbar = tqdm(as_completed(futures), total=len(param_grid), disable=not verbose)
        for future in pbar:
            run_idx, params = futures[future]
            try:
                row = future.result()
                rows.append(row)
                if verbose:
                    pbar.set_postfix(
                        run=f"run_{run_idx:05d}",
                        edge=f"{row['mm_avg_passive_edge']:.2f}",
                    )
            except Exception as exc:  # noqa: BLE001
                if verbose:
                    pbar.set_postfix(run=f"run_{run_idx:05d}", status="FAILED")
                    tqdm.write(f"run_{run_idx:05d} FAILED: {exc}")
                rows.append(
                    {**params, "mm_avg_passive_edge": float("nan"), "output_dir": "",
                     "run_id": None}
                )

    return pd.DataFrame(rows)
