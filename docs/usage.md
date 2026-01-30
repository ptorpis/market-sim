# Usage Guide

This document covers the usage of the main components in the market simulator.

## Table of Contents

- [MarketSimulator](#marketsimulator)
- [Visualize Book](#visualize-book)
- [Visualize Timeseries](#visualize-timeseries)
- [Testing](#testing)

---

## MarketSimulator

The main simulation engine. Simulates financial market microstructure with agents (noise traders, market makers, informed traders), order books, and a matching engine.

### Building

```bash
# Build all variants (debug, ASAN, Valgrind)
./run_all.sh

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
    "initial_price": 1000000,
    "drift": 0.0001,
    "volatility": 0.005,
    "tick_size": 1000,
    "seed": 43
  },
  "agents": [
    {
      "client_id": 1,
      "type": "NoiseTrader",
      "initial_wakeup": 10,
      "seed": 100,
      "config": { ... }
    }
  ]
}
```

#### Agent Types

| Type | Description |
|------|-------------|
| `NoiseTrader` | Random liquidity provider, places random buy/sell orders |
| `MarketMaker` | Quotes both sides around fair price, adjusts for inventory |
| `InformedTrader` | Trades on information signals when edge exceeds threshold |

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
| `fair` | Fair price from GBM model |
| `bid` | Best bid price |
| `ask` | Best ask price |

---

## Testing

The project uses Google Test (C++) and pytest (Python).

### Running All Tests

```bash
# Build and run all tests
./run_all.sh

# Or manually
ctest --test-dir build/debug --output-on-failure
```

### C++ Tests

```bash
# Run all C++ tests
./build/debug/all_tests

# Run specific test
./build/debug/all_tests --gtest_filter="MatchingEngine*"
```

### Python Tests

```bash
# Run visualization tool tests
pytest tests/test_visualize_book.py -v

# Run cross-validation tests
pytest tests/python/ -v
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

# 5. Run tests
./run_all.sh
```
