# Experiments

The `tools/experiments/` package provides infrastructure for running systematic parameter sweeps over the simulator to study market microstructure phenomena. The initial focus is replicating the Glosten-Milgrom (1985) prediction that the equilibrium bid-ask spread is proportional to the informed trader ratio and asset volatility.

## Table of Contents

- [Overview](#overview)
- [Package Structure](#package-structure)
- [GM Replication Experiment](#gm-replication-experiment)
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
runner.py             ← executes one simulation, returns output directory
config_builder.py     ← builds a config dict from named parameters
analysis.py           ← reads results from pnl.csv, computes breakeven and Roll estimate
```

**Dependencies:** `pandas`, `matplotlib`, `numpy` (install pandas if not already present: `pip install pandas`)

---

## Package Structure

```
tools/experiments/
├── __init__.py
├── config_builder.py    # build_gm_config() — programmatic config construction
├── runner.py            # run_simulation() — single run wrapper
├── sweep.py             # run_sweep() — parallel batch execution
├── analysis.py          # read_mm_final_pnl(), find_breakeven_spread(), roll_spread_estimator()
└── gm_replication.py    # Phase 2 GM replication entry point
```

---

## GM Replication Experiment

Sweeps over (σ, π, half\_spread) to find the market maker's break-even spread as a function of the informed trader ratio and GBM volatility. Verifies:

$$\text{equilibrium spread} \propto \pi \cdot \sigma$$

### Quick Start

```bash
# Preview the full parameter grid and a sample config without running anything
python -m tools.experiments.gm_replication --dry-run

# Run the full experiment (315 runs, ~4 parallel workers)
python -m tools.experiments.gm_replication

# Run with more parallelism and a custom output location
python -m tools.experiments.gm_replication --n-workers 8 --results-dir /tmp/gm_run1

# Resume an interrupted sweep (skips runs whose output already exists)
python -m tools.experiments.gm_replication --results-dir /tmp/gm_run1

# Force re-run everything
python -m tools.experiments.gm_replication --no-skip
```

### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--binary` | `./build/debug/MarketSimulator` | Path to the compiled binary |
| `--results-dir` | `./experiment_results/gm_replication` | Parent directory for all run outputs and summary CSVs |
| `--n-workers` | `4` | Number of parallel simulation threads |
| `--dry-run` | — | Print grid summary and a sample config, then exit |
| `--no-skip` | — | Re-run even if output already exists |
| `--seed-offset` | `0` | Added to all seed values; useful for independent replications |

### Sweep Parameters

| Parameter | Values | Description |
|-----------|--------|-------------|
| σ (sigma) | 0.001, 0.002, 0.005 | GBM volatility |
| π (pi) | 0.10, 0.20, 0.30, 0.40, 0.50 | Informed trader fraction |
| half\_spread | 50, 100, 200, 400, 700, 1000, 2000 | MM half-spread in price units |
| replicates | 3 | Independent seeds per (σ, π) combination |

Total: 3 × 5 × 7 × 3 = 315 runs.

The same seed is used for all `half_spread` values within a (σ, π, replicate) group, so the fair price path and agent arrival patterns are held fixed across the spread sweep — a controlled comparison.

### Output

All outputs are written to `--results-dir`:

```
experiment_results/gm_replication/
├── run_00000/          # individual simulation outputs (one directory per run)
│   ├── deltas.csv
│   ├── trades.csv
│   ├── pnl.csv
│   ├── market_state.csv
│   └── metadata.json
├── run_00001/
│   └── ...
├── results.csv         # one row per run: sigma, pi, half_spread, seed_base, mm_final_pnl, output_dir
├── breakeven.csv       # one row per (sigma, pi): interpolated breakeven half_spread
└── plots/
    ├── eq_spread_vs_pi.png          # equilibrium spread vs π for each σ
    ├── eq_spread_vs_sigma.png       # equilibrium spread vs σ for each π
    └── pnl_curves_sigma*.png        # MM PnL vs half_spread (one figure per σ)
```

### Interpreting Results

**`results.csv`** — raw data; each row is one simulation run. `mm_final_pnl` is the market maker's total mark-to-market PnL at the end of the simulation:

$$\text{total PnL} = \text{cash} + (\text{long\_position} - \text{short\_position}) \times \text{fair\_price}$$

**`breakeven.csv`** — for each (σ, π), the `half_spread` where MM PnL crosses zero, found by linear interpolation between the adjacent sweep values. `NaN` means no zero crossing was found within the sweep range (the spread range needs to be extended).

**PnL curves** — sanity check plots. Each curve should start negative (tight spread, MM gets adversely selected) and rise to positive (wide spread, MM earns more than it loses). The zero crossing is the equilibrium spread.

**Roll estimator** — printed to the console after the sweep. The Roll (1984) model-free spread estimate:

$$\text{spread} = 2\sqrt{-\text{Cov}(\Delta P_t,\, \Delta P_{t+1})}$$

where $\Delta P$ is the price change between consecutive trades. This should be in the same ballpark as the interpolated breakeven spread for a sanity check.

---

## Building Your Own Sweep

### config\_builder

`build_gm_config()` constructs a config dict that the C++ binary accepts directly. It mirrors `config_template.json` exactly.

```python
from tools.experiments.config_builder import build_gm_config

config = build_gm_config(
    sigma=0.005,             # GBM volatility
    pi=0.3,                  # informed trader fraction
    half_spread=400,         # MM half-spread in price units
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
| `seed_base` | Base RNG seed; noise group, MM, and fair price each get a deterministic offset from this |
| `observation_noise` | Fair price observation noise on InformedTrader (float) |
| `inventory_skew_factor` | MM inventory skew coefficient |
| `initial_price` | Starting asset price in price units (default: 1,000,000) |
| `tick_size` | Time units per GBM step (default: 1,000) |
| `drift` | GBM drift (default: 0.0) |
| `noise_trader_spread` | Price range for noise trader orders (default: `max(half_spread × 10, 500)`) |

### runner

`run_simulation()` executes one simulation from a config dict:

```python
from pathlib import Path
from tools.experiments.runner import run_simulation
from tools.experiments.config_builder import build_gm_config

config = build_gm_config(sigma=0.005, pi=0.3, half_spread=400)
output_dir = run_simulation(
    config,
    output_dir=Path("/tmp/my_run"),
    binary=Path("./build/debug/MarketSimulator"),
    timeout=300,  # seconds
)
# output_dir now contains deltas.csv, trades.csv, pnl.csv, market_state.csv, metadata.json
```

Raises `RuntimeError` on non-zero binary exit and `subprocess.TimeoutExpired` if the run exceeds `timeout` seconds.

### sweep

`run_sweep()` runs a list of parameter dicts in parallel and returns a DataFrame:

```python
from pathlib import Path
from tools.experiments.sweep import run_sweep, parameter_grid

# Build a grid from lists of values
grid = parameter_grid(
    sigma=[0.001, 0.005],
    pi=[0.2, 0.3, 0.4],
    half_spread=[100, 400, 1_000],
    # remaining kwargs forwarded to build_gm_config with defaults
    n_total_traders=[20],
    duration=[500_000],
    pnl_snapshot_interval=[1_000],
    seed_base=[42],
    observation_noise=[0.0],
    inventory_skew_factor=[0.0],
)

results = run_sweep(
    grid,
    results_dir=Path("./my_experiment"),
    binary=Path("./build/debug/MarketSimulator"),
    n_workers=4,
    skip_existing=True,   # resume safely if interrupted
    verbose=True,
)
# results is a DataFrame with one row per run
```

Each run's output is written to `results_dir/run_XXXXX/`. Failed runs produce a row with `mm_final_pnl=NaN`. The sweep is safely resumable: runs whose `pnl.csv` already exists are skipped when `skip_existing=True`.

### analysis

```python
from pathlib import Path
from tools.experiments.analysis import (
    read_mm_final_pnl,
    find_breakeven_spread,
    roll_spread_estimator,
)

# Read MM final PnL from a completed run
pnl = read_mm_final_pnl(Path("./my_experiment/run_00000"), mm_client_id=999)

# Find the break-even half_spread by interpolation
breakeven = find_breakeven_spread({
    50: -15_000,
    100: -8_000,
    200: 1_500,    # zero crossing is between 100 and 200
    400: 12_000,
})
# breakeven ≈ 184.6

# Roll (1984) spread estimate from a trades.csv
roll = roll_spread_estimator(Path("./my_experiment/run_00000/trades.csv"))
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
