# Usage Guide

This document covers the usage of the main components in the market simulator.

## Table of Contents

- [MarketSimulator](#marketsimulator)
- [Visualize Book](#visualize-book)
- [Visualize Timeseries](#visualize-timeseries)
- [Analyze Adverse Selection](#analyze-adverse-selection)
- [Testing](#testing)
  - [Cross-Validation Harness](#cross-validation-harness)

---

## MarketSimulator

The main simulation engine. Simulates financial market microstructure with agents (noise traders, market makers, informed traders), order books, and a matching engine.

### Building

```bash
# Quick debug build and test only
./build.sh --debug

# Full build: debug + ASAN + Valgrind + Python tests
./build.sh

# Manual debug build
cmake -S . -B build/debug
cmake --build build/debug --parallel 4
```

### Running

```bash
# Use default config (looks for config.json then config_template.json)
./build/debug/MarketSimulator

# Use specific config file
./build/debug/MarketSimulator --config my_config.json

# Override output directory
./build/debug/MarketSimulator --config config.json --output /tmp/sim_output
```

### Configuration

Configuration is via JSON file. See `config_template.json` for a complete example.

```json
{
  "simulation": {
    "latency": 10,
    "duration": 1000000,
    "output_dir": "./output",
    "pnl_snapshot_interval": 100
  },
  "instruments": [1],
  "fair_price": {
    "model": "gbm",
    "initial_price": 1000000,
    "drift": 0.0001,
    "volatility": 0.005,
    "tick_size": 1000,
    "seed": 43
  },
  "noise_traders": {
    "count": 100,
    "start_client_id": 1,
    "base_seed": 100,
    "initial_wakeup_start": 10,
    "initial_wakeup_step": 10,
    "config": {
      "latency_jitter": 0.3,
      ...
    }
  },
  "agents": [
    {
      "client_id": 200,
      "type": "MarketMaker",
      "initial_wakeup": 5,
      "seed": 999,
      "config": {
        "latency_jitter": 0.2,
        ...
      }
    }
  ]
}
```

#### Noise Trader Group

The `noise_traders` section creates multiple noise traders with shared parameters:

| Field | Description |
|-------|-------------|
| `count` | Number of noise traders to create |
| `start_client_id` | First trader gets this ID, subsequent traders get ID+1, ID+2, etc. |
| `base_seed` | First trader uses this seed, subsequent traders use seed+1, seed+2, etc. |
| `initial_wakeup_start` | Timestamp for first trader's initial wakeup |
| `initial_wakeup_step` | Stagger between wakeups (trader N wakes at start + N*step) |
| `config` | Shared `NoiseTraderConfig` for all traders |

This allows scaling to hundreds of noise traders without verbose config files. Individual noise traders can still be added in the `agents` array for special cases.

#### Agent Types

| Type | Description |
|------|-------------|
| `NoiseTrader` | Random liquidity provider, places random buy/sell orders |
| `MarketMaker` | Quotes both sides around fair price, adjusts for inventory |
| `InformedTrader` | Trades on information signals when edge exceeds threshold |

#### Latency Jitter

Each agent type config supports a `latency_jitter` parameter that adds realistic variability to order-to-exchange latency using a log-normal distribution.

When `latency_jitter` is set to a value greater than 0, each action's latency is sampled from:

$$\text{latency} \sim \text{LogNormal}(\ln(\text{base\_latency}),\; \sigma)$$

Where `base_latency` is the agent's configured latency (or the global default) and $\sigma$ is the `latency_jitter` value. The median of the distribution equals the base latency, so the jitter adds noise around it with occasional high-latency spikes. Each agent's jitter RNG is seeded from its own seed for reproducibility.

| `latency_jitter` | Effect |
|-------------------|--------|
| `0` (default) | No jitter, constant latency |
| `0.1` | Mild jitter (~10% variation) |
| `0.3` | Moderate jitter |
| `0.5` | High jitter with occasional large spikes |

The parameter is set inside each agent type's `config` block:

```json
"config": {
  "latency_jitter": 0.3,
  ...
}
```

#### Fair Price Models

The `fair_price` section controls how the true underlying asset price evolves. Two models are available:

##### Geometric Brownian Motion (GBM)

The default model. Price follows continuous diffusion with constant drift and volatility:

$$\frac{dS}{S} = \mu \, dt + \sigma \, dW$$

```json
"fair_price": {
  "model": "gbm",
  "initial_price": 1000000,
  "drift": 0.0001,
  "volatility": 0.005,
  "tick_size": 1000,
  "seed": 43
}
```

| Field | Description |
|-------|-------------|
| `model` | `"gbm"` (optional, default if omitted) |
| `initial_price` | Starting price in integer units |
| `drift` | Expected return per tick ($\mu$) |
| `volatility` | Price volatility per tick ($\sigma$) |
| `tick_size` | Time unit for $dt$ calculation |
| `seed` | Random seed for reproducibility |

##### Jump Diffusion Model

Extends GBM with random jumps for modeling sudden price movements. Useful for studying adverse selection where informed traders exploit price jumps.

The model follows Merton's Jump Diffusion:

$$\frac{dS}{S} = (\mu - \lambda k) \, dt + \sigma \, dW + J \, dN$$

Where:
- $dW$ is a Wiener process (continuous diffusion)
- $dN$ is a Poisson process with intensity $\lambda$ (jump arrivals)
- $J = e^{\mu_J + \sigma_J Z} - 1$ is the random jump size ($Z \sim N(0,1)$)
- $k = E[e^J] - 1 = e^{\mu_J + \frac{1}{2}\sigma_J^2} - 1$ compensates for jump risk in the drift

```json
"fair_price": {
  "model": "jump_diffusion",
  "initial_price": 1000000,
  "drift": 0.0001,
  "volatility": 0.005,
  "tick_size": 1000,
  "jump_intensity": 0.1,
  "jump_mean": 0.0,
  "jump_std": 0.05,
  "seed": 43
}
```

| Field | Description |
|-------|-------------|
| `model` | `"jump_diffusion"` |
| `initial_price` | Starting price in integer units |
| `drift` | Expected return per tick ($\mu$) |
| `volatility` | Diffusion volatility per tick ($\sigma$) |
| `tick_size` | Time unit for $dt$ calculation |
| `jump_intensity` | Mean number of jumps per tick ($\lambda$) |
| `jump_mean` | Mean of log-jump sizes ($\mu_J$) |
| `jump_std` | Standard deviation of log-jump sizes ($\sigma_J$) |
| `seed` | Random seed for reproducibility |

**Recommended settings for adverse selection studies:**
- Higher `jump_intensity` (0.1-0.5) for more frequent jumps
- Higher `jump_std` (0.05-0.2) for larger jump magnitudes
- Set `jump_mean` to 0 for symmetric jumps, negative for crash scenarios

### Output Files

Generated in `output_dir/`:

| File | Description |
|------|-------------|
| `deltas.csv` | Order book change events (ADD, FILL, CANCEL, MODIFY) |
| `trades.csv` | All executed trades |
| `pnl.csv` | Periodic P&L snapshots per agent |
| `market_state.csv` | Fair price and best bid/ask at each timestamp |
| `metadata.json` | Simulation configuration and agent details |

---

## Visualize Book

Reconstructs and navigates order book state at any timestamp by replaying delta events.

### Usage

```bash
# Show final order book state
python tools/visualize_book.py output/deltas.csv

# Show order book at specific timestamp
python tools/visualize_book.py output/deltas.csv --at 1000

# Interactive mode
python tools/visualize_book.py output/deltas.csv -i

# Show depth chart
python tools/visualize_book.py output/deltas.csv --plot

# Save depth chart to file
python tools/visualize_book.py output/deltas.csv --plot-output depth.png

# Control price levels displayed (default: 10)
python tools/visualize_book.py output/deltas.csv --levels 5
python tools/visualize_book.py output/deltas.csv --levels max
```

### Arguments

| Argument | Short | Default | Description |
|----------|-------|---------|-------------|
| `deltas_file` | - | required | Path to deltas.csv file |
| `--at` | - | - | Show order book at specific timestamp |
| `--interactive` | `-i` | - | Interactive mode |
| `--plot` | - | - | Show depth chart |
| `--plot-output` | - | - | Save depth chart to file |
| `--levels` | - | 10 | Number of price levels (or 'max') |

### Interactive Commands

| Command | Description |
|---------|-------------|
| `<timestamp>` | Jump to timestamp |
| `n` | Next timestamp |
| `p` | Previous timestamp |
| `o <order_id>` | Inspect specific order |
| `l <BUY\|SELL> <price>` | List orders at price level |
| `t` | Inspect top of book |
| `d <num>\|<max>` | Adjust depth being displayed |
| `q` | Quit |
| `h` | Help |

---

## Visualize Timeseries

Plots spread, midpoint, and fair price evolution over time to analyze price discovery and market efficiency.

### Usage

```bash
# Plot all metrics
python tools/visualize_timeseries.py output/

# Plot specific metrics
python tools/visualize_timeseries.py output/ --metric spread
python tools/visualize_timeseries.py output/ -m mid -m fair

# Save to file
python tools/visualize_timeseries.py output/ -o plot.png

# Sample data for faster plotting
python tools/visualize_timeseries.py output/ --sample 1000

# Comprehensive price discovery analysis (3-panel layout)
python tools/visualize_timeseries.py output/ --analysis

# Custom title
python tools/visualize_timeseries.py output/ --title "My Simulation"
```

### Arguments

| Argument | Short | Default | Description |
|----------|-------|---------|-------------|
| `output_dir` | - | required | Path to simulation output directory |
| `--metric` | `-m` | all | Metrics to plot: mid, spread, fair, bid, ask, all |
| `--analysis` | `-a` | - | Show comprehensive analysis view |
| `--output` | `-o` | - | Save plot to file |
| `--sample` | `-s` | - | Sample N points for faster plotting |
| `--title` | `-t` | - | Custom plot title |

### Metrics

| Metric | Description |
|--------|-------------|
| `mid` | Midpoint = (best_bid + best_ask) / 2 |
| `spread` | best_ask - best_bid |
| `fair` | Fair price from the configured model (GBM or Jump Diffusion) |
| `bid` | Best bid price |
| `ask` | Best ask price |

---

## Analyze Adverse Selection

Measures how the age of a market maker's resting quote relates to the adverse selection experienced when that quote is filled. Tests the stale-quote hypothesis: older quotes that haven't been updated to reflect the latest fair price should suffer worse adverse selection.

The tool reads simulation output files (`trades.csv`, `deltas.csv`, `market_state.csv`, `metadata.json`), identifies fills where the MM was the resting (maker) side, and computes per-fill quote age, immediate adverse selection, and realized adverse selection at configurable future horizons.

### Usage

```bash
# Basic analysis (auto-detects MM if only one exists)
python -m tools.analyze_adverse_selection output/

# Specify market maker
python -m tools.analyze_adverse_selection output/ --mm-id 200

# Show scatter and horizon plots
python -m tools.analyze_adverse_selection output/ --plot

# Save plot to file
python -m tools.analyze_adverse_selection output/ --plot --output as_plot.png

# Custom realized AS horizons
python -m tools.analyze_adverse_selection output/ --horizons 50 100 200 500

# More quote age buckets
python -m tools.analyze_adverse_selection output/ --buckets 6

# Skip writing per-fill CSV
python -m tools.analyze_adverse_selection output/ --no-csv
```

### Arguments

| Argument | Short | Default | Description |
|----------|-------|---------|-------------|
| `output_dir` | - | required | Directory containing simulation output files |
| `--mm-id` | - | auto | Market maker client_id (auto-detected from metadata.json if only one MM) |
| `--horizons` | - | 50 100 200 500 | Realized AS horizons in ticks |
| `--buckets` | - | 4 | Number of quote age buckets (quartile-based) |
| `--plot` | - | - | Show scatter and horizon plots |
| `--output` | - | - | Save plot to file |
| `--csv` | - | `OUTPUT_DIR/adverse_selection.csv` | Custom path for per-fill CSV |
| `--no-csv` | - | - | Skip writing per-fill CSV |

### Key Metrics

| Metric | Formula (MM bought) | Formula (MM sold) |
|--------|--------------------|--------------------|
| Quote age | `fill_timestamp - order_birth_timestamp` | same |
| Immediate AS | `fair_price - fill_price` | `fill_price - fair_price` |
| Realized AS(h) | `fair_price(t+h) - fill_price` | `fill_price - fair_price(t+h)` |

Negative adverse selection means the price moved against the MM (they overpaid on a buy or sold too cheap). The order birth timestamp resets on MODIFY events, since the MM actively updated the quote.

### Output

**Console summary** — per-bucket table showing fill count, mean/median immediate AS, realized AS at a representative horizon, and percentage of fills against informed traders.

**Per-fill CSV** (`adverse_selection.csv`) — one row per MM maker fill with columns: `fill_timestamp`, `trade_id`, `mm_order_id`, `mm_side`, `quote_age`, `fill_price`, `fair_price`, `immediate_as`, `realized_as_<horizon>` (one per horizon), `counterparty_id`, `counterparty_type`.

**Plots** (with `--plot`):
1. Scatter of immediate AS vs quote age, colored by counterparty type, with binned means overlay
2. Line plot of realized AS at multiple horizons vs quote age

### Example

```bash
# Run simulation then analyze
./build/debug/MarketSimulator
python -m tools.analyze_adverse_selection output/ --plot
```

```
Adverse Selection Analysis (MM client_id=10)
============================================================
Total MM fills: 1103 (maker only)
  vs InformedTrader: 15 (1.4%)
  vs NoiseTrader: 1088 (98.6%)

By Quote Age:
  Bucket         | Count | Mean Imm. AS | Med Imm. AS |  Mean AS@200 | % Informed
  -------------------------------------------------------------------------------
  [0, 12)        |   263 |        -80.6 |       -41.0 |       -381.1 |       2.7%
  [12, 31)       |   278 |       -639.7 |      -419.0 |       -719.3 |       2.2%
  [31, 62)       |   284 |       -851.2 |      -590.0 |       -775.7 |       0.7%
  [62, inf)      |   278 |      -1835.4 |      -992.5 |      -3659.0 |       0.0%
```

---

## Testing

The project uses Google Test (C++) and pytest (Python).

### Running All Tests

```bash
# Quick: debug build and tests only
./build.sh --debug

# Full: all build variants + Python tests
./build.sh

# Or manually
ctest --test-dir build/debug --output-on-failure
```

### C++ Tests

```bash
# Run all C++ tests
./build/debug/unit_tests

# Run specific test
./build/debug/unit_tests --gtest_filter="MatchingEngine*"
```

### Python Tests

```bash
# Run visualization tool tests
pytest tests/test_visualize_book.py -v

# Run adverse selection cross-validation tests
pytest tests/test_adverse_selection.py -v

# Run cross-validation infrastructure tests
pytest tests/python/ -v
```

### Cross-Validation Harness

The cross-validation harness validates that the Python replay engine produces identical state to the C++ simulation. It orchestrates end-to-end testing by:

1. Running C++ `cross_validation_tests` with state export enabled
2. Replaying deltas in Python for each test scenario
3. Comparing Python state with C++ exported state
4. Reporting any differences

```bash
# Run cross-validation (uses build/debug by default)
python -m tools.testing.harness

# Specify build directory
python -m tools.testing.harness --build-dir build/debug

# Verbose output (shows C++ test output)
python -m tools.testing.harness --build-dir build/debug --verbose

# Keep output directories for inspection
python -m tools.testing.harness --build-dir build/debug --keep-output
```

#### Arguments

| Argument | Description |
|----------|-------------|
| `--build-dir` | Build directory containing `cross_validation_tests` binary |
| `--verbose`, `-v` | Print detailed progress and C++ output |
| `--keep-output` | Preserve test output directories for debugging |

#### Example Output

```
============================================================
Running C++ cross-validation tests...
============================================================

============================================================
Validating test outputs with Python...
============================================================
[PASS] test_0 (6 states validated)
[PASS] test_1 (5 states validated)
[PASS] test_2 (4 states validated)

============================================================
Cross-Validation Summary
============================================================
Binary: build/debug/cross_validation_tests
Total tests: 3
  Passed: 3
  Failed: 0
  Errors: 0
  Skipped: 0

ALL CROSS-VALIDATION TESTS PASSED
```

### Build Variants

```bash
# Debug build
cmake -S . -B build/debug

# ASAN/UBSAN build (memory/undefined behavior detection)
cmake -S . -B build/asan -DENABLE_ASAN=ON -DENABLE_UBSAN=ON

# Valgrind build
cmake -S . -B build/valgrind -DENABLE_VALGRIND=ON
```

---

## Quick Start

```bash
# 1. Build
cmake -S . -B build/debug && cmake --build build/debug

# 2. Run simulation
./build/debug/MarketSimulator

# 3. Explore order book interactively
python tools/visualize_book.py output/deltas.csv -i

# 4. Analyze price discovery
python tools/visualize_timeseries.py output/ --analysis

# 5. Analyze adverse selection
python -m tools.analyze_adverse_selection output/ --plot

# 6. Run tests
./build.sh --debug
```
