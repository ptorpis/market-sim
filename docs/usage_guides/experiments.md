# Experiments

The `tools/experiments/` package provides infrastructure for running systematic parameter sweeps over the simulator to study market microstructure phenomena. The initial focus is replicating the Glosten-Milgrom (1985) prediction that the equilibrium bid-ask spread is proportional to the informed trader ratio and asset volatility.

## Table of Contents

- [Overview](#overview)
- [Package Structure](#package-structure)
- [GM Replication Experiment](#gm-replication-experiment)
- [GM Replication v2](#gm-replication-v2)
- [Building Your Own Sweep](#building-your-own-sweep)
  - [config\_builder](#config_builder)
  - [runner](#runner)
  - [sweep](#sweep)
  - [analysis](#analysis)

---

## Overview

The experiment infrastructure has four layers:

```
gm_replication.py     ← entry point; defines the specific sweep and produces plots
sweep.py              ← runs a parameter grid in parallel, returns a DataFrame
runner.py             ← executes one simulation, writes output to disk (and/or DB)
config_builder.py     ← builds a config dict from named parameters
analysis.py           ← reads results from CSV or DB, computes breakeven and Roll estimate
```

**Dependencies:** `pandas`, `matplotlib`, `numpy`, `psycopg2-binary` (for DB mode)

---

## Package Structure

```
tools/experiments/
├── __init__.py
├── config_builder.py      # build_gm_config() — programmatic config construction
├── runner.py              # run_simulation() — single run wrapper
├── sweep.py               # run_sweep() — parallel batch execution
├── analysis.py            # read_mm_passive_edge(), find_breakeven_spread(), roll_spread_estimator()
├── gm_replication.py      # Phase 2 GM replication entry point (v1)
└── gm_replication_v2.py   # Higher-resolution grid, 10 replicates, DB-backed (v2)
```

---

## GM Replication Experiment

Sweeps over (σ, π, half\_spread) to find the market maker's break-even spread as a function of the informed trader ratio and GBM volatility. Verifies:

$$\text{equilibrium spread} \propto \pi \cdot \sigma$$

### Quick Start

```bash
# Preview the full parameter grid and a sample config without running anything
python -m tools.experiments.gm_replication --dry-run --results-dir /tmp/gm_v1

# Run the full experiment (315 runs, 4 parallel workers)
python -m tools.experiments.gm_replication --results-dir /tmp/gm_v1

# Run with more parallelism
python -m tools.experiments.gm_replication --n-workers 8 --results-dir /tmp/gm_v1

# Resume an interrupted sweep (skips runs whose output already exists)
python -m tools.experiments.gm_replication --results-dir /tmp/gm_v1

# Force re-run everything
python -m tools.experiments.gm_replication --results-dir /tmp/gm_v1 --no-skip
```

### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--binary` | `./build/debug/MarketSimulator` | Path to the compiled binary |
| `--results-dir` | **required** | Parent directory for all run outputs and summary CSVs |
| `--n-workers` | `4` | Number of parallel simulation threads |
| `--dry-run` | — | Print grid summary and a sample config, then exit |
| `--no-skip` | — | Re-run even if output already exists |
| `--seed-offset` | `0` | Added to all seed values; useful for independent replications |

### Sweep Parameters

| Parameter | Values | Description |
|-----------|--------|-------------|
| σ (sigma) | 0.001, 0.002, 0.005 | GBM volatility |
| π (pi) | 0.10, 0.20, 0.30, 0.40, 0.50 | Informed trader fraction |
| half\_spread | 5, 10, 20, 40, 80, 150, 300 | MM half-spread in price units |
| replicates | 3 | Independent seeds per (σ, π) combination |

Total: 3 × 5 × 7 × 3 = 315 runs.

The same seed is used for all `half_spread` values within a (σ, π, replicate) group, so the fair price path and agent arrival patterns are held fixed across the spread sweep — a controlled comparison.

### Output

All outputs are written to `--results-dir`:

```
<results-dir>/
├── run_00000/          # individual simulation outputs (one directory per run)
│   ├── deltas.csv
│   ├── trades.csv
│   ├── pnl.csv
│   ├── market_state.csv
│   └── metadata.json
├── run_00001/
│   └── ...
├── results.csv         # one row per run: sigma, pi, half_spread, mm_avg_passive_edge, output_dir
├── breakeven.csv       # one row per (sigma, pi): interpolated breakeven half_spread
└── plots/
    ├── eq_spread_vs_pi.png          # equilibrium spread vs π for each σ
    ├── eq_spread_vs_sigma.png       # equilibrium spread vs σ for each π
    └── pnl_curves_sigma*.png        # MM passive edge vs half_spread (one figure per σ)
```

### Interpreting Results

**Metric:** `mm_avg_passive_edge` — average per-trade passive edge relative to fair price:

$$\text{edge} = \begin{cases} p - p_\text{fair} & \text{MM sell (aggressor = buyer)} \\ p_\text{fair} - p & \text{MM buy (aggressor = seller)} \end{cases}$$

Positive edge = spread income exceeds adverse selection. The **breakeven half-spread** is where this crosses zero.

**`breakeven.csv`** — for each (σ, π), the `half_spread` where the edge crosses zero, found by linear interpolation. `NaN` means no zero crossing was found within the sweep range.

**PnL curves** — each curve should start negative (tight spread, MM gets adversely selected) and rise to positive (wide spread, MM earns more than it loses). The zero crossing is the equilibrium spread.

**Roll estimator** — printed to the console after the sweep. Model-free spread estimate from the serial correlation of trade price changes:

$$\text{spread} = 2\sqrt{-\text{Cov}(\Delta P_t,\, \Delta P_{t+1})}$$

---

## GM Replication v2

Addresses two caveats from the v1 results:

1. **σ = 0.005 resolution:** adds half\_spread values 175, 200, 225 in the [150, 300] bracket where all v1 breakevens fell
2. **Replicate count:** 10 replicates per cell (vs. 3) for tighter confidence intervals

The first 3 replicates use identical seeds to v1 for direct comparability. Results include 95% CI bands on PnL curve plots.

### Quick Start

```bash
# Dry run
python -m tools.experiments.gm_replication_v2 --dry-run --results-dir /tmp/gm_v2

# Full sweep with DB persistence (default)
python -m tools.experiments.gm_replication_v2 \
    --results-dir /tmp/gm_v2 \
    --n-workers 7

# CSV only (no DB)
python -m tools.experiments.gm_replication_v2 \
    --results-dir /tmp/gm_v2 \
    --no-db \
    --n-workers 7

# Resume interrupted sweep
python -m tools.experiments.gm_replication_v2 --results-dir /tmp/gm_v2 --n-workers 7
```

### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--binary` | `./build/release/MarketSimulator` | Path to the compiled binary |
| `--results-dir` | **required** | Parent directory for run_id.txt files and summary CSVs/plots |
| `--n-workers` | `6` | Number of parallel simulation threads |
| `--db-conn` | `postgresql://localhost:5433/market_sim?host=/tmp` | PostgreSQL connection string |
| `--no-db` | — | Disable PostgreSQL persistence (CSV only) |
| `--dry-run` | — | Print grid summary and a sample config, then exit |
| `--no-skip` | — | Re-run even if output already exists |
| `--seed-offset` | `0` | Added to all seed values |

### Sweep Parameters

| Parameter | Values | Description |
|-----------|--------|-------------|
| σ (sigma) | 0.001, 0.002, 0.005 | GBM volatility |
| π (pi) | 0.10, 0.20, 0.30, 0.40, 0.50 | Informed trader fraction |
| half\_spread | 5, 10, 20, 40, 80, 150, 175, 200, 225, 300 | MM half-spread in price units |
| replicates | 10 | Independent seeds per (σ, π) combination |

Total: 3 × 5 × 10 × 10 = 1,500 runs.

### Output (DB mode)

In DB mode (default), each run writes only a `run_id.txt` to `results-dir/run_XXXXX/`. All simulation data goes to PostgreSQL. Summary files are written to `results-dir`:

```
<results-dir>/
├── run_00000/
│   └── run_id.txt      # UUID of the PostgreSQL run
├── run_00001/
│   └── ...
├── results.csv
├── breakeven.csv
└── plots/
    ├── eq_spread_vs_pi.png
    ├── eq_spread_vs_sigma.png
    └── pnl_curves_sigma*.png   # includes 95% CI bands from 10 replicates
```

---

## Building Your Own Sweep

### config\_builder

`build_gm_config()` constructs a config dict that the C++ binary accepts directly.

```python
from tools.experiments.config_builder import build_gm_config

config = build_gm_config(
    sigma=0.005,             # GBM volatility
    pi=0.3,                  # informed trader fraction
    half_spread=80,          # MM half-spread in price units
    n_total_traders=20,      # noise + informed (MM excluded)
    duration=500_000,
    pnl_snapshot_interval=1_000,
    seed_base=42,
    observation_noise=0.0,       # 0.0 = perfectly informed (cleanest GM analog)
    inventory_skew_factor=0.0,   # 0.0 = pure GM, no inventory adjustment
)
```

The MM is always assigned `client_id=999`. Noise traders occupy IDs 1 to `n_noise`. Informed traders start at ID 500.

| Parameter | Description |
|-----------|-------------|
| `sigma` | GBM volatility |
| `pi` | Informed fraction of total trader population (0 < π < 1) |
| `half_spread` | MM half-spread in price units |
| `n_total_traders` | Total noise + informed count (MM excluded) |
| `duration` | Simulation duration in ticks |
| `pnl_snapshot_interval` | Ticks between PnL snapshots |
| `seed_base` | Base RNG seed; each agent group and fair price get a deterministic offset |
| `observation_noise` | Fair price observation noise on InformedTrader (float) |
| `inventory_skew_factor` | MM inventory skew coefficient |
| `initial_price` | Starting asset price in price units (default: 1,000,000) |
| `tick_size` | Time units per GBM step (default: 1,000) |
| `drift` | GBM drift (default: 0.0) |
| `noise_trader_spread` | Price range for noise trader orders (default: `max(half_spread × 10, 500)`) |

### runner

`run_simulation()` executes one simulation from a config dict. Pass `db_connection_string` to stream output to PostgreSQL (postgres-only backend); omit it for CSV output.

```python
from pathlib import Path
from tools.experiments.runner import run_simulation
from tools.experiments.config_builder import build_gm_config

config = build_gm_config(sigma=0.005, pi=0.3, half_spread=80)

# CSV mode
output_dir = run_simulation(
    config,
    output_dir=Path("/tmp/my_run"),
    binary=Path("./build/release/MarketSimulator"),
    timeout=300,
)
# output_dir contains deltas.csv, trades.csv, pnl.csv, market_state.csv, metadata.json

# DB mode (postgres-only, no CSV output)
output_dir = run_simulation(
    config,
    output_dir=Path("/tmp/my_run"),
    binary=Path("./build/release/MarketSimulator"),
    db_connection_string="postgresql://localhost:5433/market_sim?host=/tmp",
)
# output_dir contains only run_id.txt; all data is in PostgreSQL
```

### sweep

`run_sweep()` runs a list of parameter dicts in parallel and returns a DataFrame. In DB mode, the passive edge is read from the database via the `run_id` written to each run directory.

```python
from pathlib import Path
from tools.experiments.sweep import run_sweep, parameter_grid

grid = parameter_grid(
    sigma=[0.001, 0.005],
    pi=[0.2, 0.3, 0.4],
    half_spread=[20, 80, 150],
    n_total_traders=[20],
    duration=[500_000],
    pnl_snapshot_interval=[1_000],
    seed_base=[42],
    observation_noise=[0.0],
    inventory_skew_factor=[0.0],
)

# CSV mode
results = run_sweep(
    grid,
    results_dir=Path("/tmp/my_experiment"),
    binary=Path("./build/release/MarketSimulator"),
    n_workers=4,
    skip_existing=True,
)

# DB mode
results = run_sweep(
    grid,
    results_dir=Path("/tmp/my_experiment"),
    binary=Path("./build/release/MarketSimulator"),
    n_workers=4,
    skip_existing=True,
    db_connection_string="postgresql://localhost:5433/market_sim?host=/tmp",
)
```

The returned DataFrame has one row per run with columns: all params + `mm_avg_passive_edge` + `run_id` + `output_dir`. Failed runs have `mm_avg_passive_edge=NaN`.

**Skip behaviour:**
- CSV mode: skips if `run_dir/trades.csv` exists
- DB mode: skips if `run_dir/run_id.txt` exists (safe to resume after interruption)

### analysis

```python
from pathlib import Path
from tools.experiments.analysis import (
    read_mm_passive_edge,
    read_mm_passive_edge_db,
    find_breakeven_spread,
    roll_spread_estimator,
)

# Read MM passive edge from a completed CSV run
edge = read_mm_passive_edge(Path("/tmp/my_experiment/run_00000"), mm_client_id=999)

# Read MM passive edge from DB
edge = read_mm_passive_edge_db(
    run_id="<uuid>",
    conn_str="postgresql://localhost:5433/market_sim?host=/tmp",
)

# Find the break-even half_spread by interpolation
breakeven = find_breakeven_spread({
    20: -8.5,
    40: -2.1,
    80: 4.3,    # zero crossing between 40 and 80
    150: 11.0,
})
# breakeven ≈ 57.6

# Roll (1984) spread estimate from a trades.csv
roll = roll_spread_estimator(Path("/tmp/my_experiment/run_00000/trades.csv"))
# Returns None if autocovariance is non-negative (no bid-ask bounce detected)
```

---

## Extension Experiments

The same infrastructure supports the GM assumption relaxation experiments from the research direction. Create a new entry-point script (e.g. `tools/experiments/ext_inventory.py`) that overrides specific parameters:

| GM Assumption | Parameter to vary | Notes |
|---------------|-------------------|-------|
| No inventory risk | `inventory_skew_factor` | Sweep [0.0, 0.5, 1.0] alongside the spread |
| Static price | `drift` | Non-zero drift; check if equilibrium spread shifts |
| No jumps | Switch fair price model to `jump_diffusion` | Requires extending `build_gm_config` |
| No latency | `latency_jitter` on informed traders | Asymmetric latency disadvantages the MM |
| Perfect information | `observation_noise` on InformedTrader | Tests signal quality vs. fraction effects |
