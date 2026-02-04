# Claude Code Guide for Market Simulator

This document provides context for AI agents working on the market-sim codebase.

## Project Overview

A discrete-time, event-driven market microstructure simulation framework. The core engine is written in modern C++23 with Python tools for analysis and visualization.

**Key characteristics:**
- Research-focused: correctness and code quality over performance
- Elaborate testing suite with C++ (Google Test) and Python (pytest)
- Strong typing discipline throughout
- Strict compiler warnings treated as errors

## Repository Structure

```
market-sim/
├── include/                 # C++ headers (main source code)
│   ├── agents/              # Trading agents (NoiseTrader, MarketMaker, InformedTrader)
│   ├── config/              # Configuration structures and JSON loader
│   ├── exchange/            # Order book types and matching engine
│   ├── persistence/         # CSV output and data collection
│   ├── simulation/          # Core engine, scheduler, events, fair price
│   ├── testing/             # Test infrastructure (state exporter, harness)
│   └── utils/               # Strong type system
├── src/                     # C++ implementations
│   └── exchange/            # Matching engine implementation
├── tests/                   # C++ test files (Google Test)
├── tools/                   # Python analysis and visualization
│   ├── visualizer/          # Order book reconstruction library
│   └── testing/             # Cross-validation framework
├── docs/                    # Documentation
├── CMakeLists.txt           # Build configuration
└── config_template.json     # Example simulation configuration
```

## Strong Type System (Critical)

**Always use strong types instead of raw integers.** Defined in `include/utils/types.hpp`:

```cpp
Timestamp   // Event scheduling (uint64_t)
Price       // Market prices (uint64_t)
Quantity    // Order quantities (uint64_t)
OrderID     // Order identifiers (uint64_t)
TradeID     // Trade identifiers (uint64_t)
ClientID    // Agent identifiers (uint64_t)
InstrumentID // Security identifiers (uint32_t)
Cash        // P&L tracking (int64_t, signed)
EventSequenceNumber // Event ordering (uint64_t)
```

**Usage patterns:**
```cpp
// Creation
Price price{1000};
Quantity qty{100};

// Access underlying value
auto raw = price.value();

// Arithmetic works between same types
Price total = price + Price{500};

// Comparisons
if (qty.is_zero()) { ... }
if (price > Price{500}) { ... }
```

## Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Functions/variables | `snake_case` | `submit_order()`, `order_id` |
| Classes/structs | `PascalCase` | `MatchingEngine`, `OrderBook` |
| Config structs | Suffix with `Config` | `NoiseTraderConfig` |
| Boolean methods | Prefix `is_` or suffix `needs_` | `is_zero()`, `needs_price_check()` |
| Private members | Suffix with `_` | `data_`, `config_` |
| Private/impl functions | Suffix with `_` | `add_to_book_()` |

## Code Quality Requirements

- Use `constexpr` where possible
- Add `[[nodiscard]]` for important return values
- Use `[[maybe_unused]]` for unused parameters in virtual overrides
- Prefer `std::optional` over sentinel values
- Use `std::variant` and `std::visit` for type-safe unions
- Prefer annotations or `std::ignore` instead of void cast; avoid C-style casts
- Use the same type and minimize casts when doing calculations
- All warnings are errors (`-Werror`)

## Build System

```bash
# Build and test all variants (debug, ASAN, Valgrind)
./run_all.sh

# Manual debug build
cmake -S . -B build/debug
cmake --build build/debug --parallel 4

# Run tests
ctest --test-dir build/debug --output-on-failure

# Run specific test suite
./build/debug/all_tests --gtest_filter="MatchingEngine*"
```

**Build variants:**
- `debug`: Standard debug build with `-g -O0`
- `asan`: AddressSanitizer + UndefinedBehaviorSanitizer
- `valgrind`: Memory leak checking

**Requirements:** CMake 3.28+, C++23 compiler (currently using g++15.2/clang++-18)

## Architecture Overview

```
SimulationEngine (orchestrator, implements AgentContext)
├── Scheduler (priority queue of events by timestamp)
├── Agents (trading strategies: NoiseTrader, MarketMaker, InformedTrader)
├── MatchingEngine(s) (per-instrument order books)
├── FairPriceGenerator (GBM model)
└── DataCollector (CSV output)
```

**Data flow:**
```
Agent → AgentContext.submit_order() → Scheduler queues event
                                            ↓
                          Scheduler pops next event by (timestamp, seq_no)
                                            ↓
                    SimulationEngine dispatches via std::visit
                                            ↓
                    MatchingEngine processes order
                                            ↓
                    Agent callbacks triggered (on_trade, on_order_accepted, etc.)
```

## Key Design Patterns

1. **Policy-Based Design** (MatchingEngine): Template parameters configure matching behavior
2. **Dispatch Table**: Function pointer table maps (OrderType, Side) → handler
3. **Visitor Pattern**: Events dispatched via `std::visit`
4. **CRTP**: Strong types use Curiously Recurring Template Pattern
5. **Observer Pattern**: Agent callbacks for order/trade events

## Key Files to Understand First

1. `include/utils/types.hpp` - Strong type system foundation
2. `include/exchange/matching_engine.hpp` - Core order matching logic
3. `include/simulation/simulation_engine.hpp` - Main orchestrator
4. `include/simulation/agent.hpp` - Agent interface and callbacks
5. `include/simulation/events.hpp` - Event types (`std::variant`)
6. `src/main.cpp` - Working simulation example

## Testing

**C++ tests** (Google Test) in `tests/`:
- `matching_engine_tests.cpp` - Order matching, FIFO, cancellations
- `scheduler_tests.cpp` - Event ordering, sequence numbers
- `simulation_engine_tests.cpp` - Integration tests
- `persistence_tests.cpp` - CSV output
- `config_loader_tests.cpp` - JSON parsing
- `fair_price_tests.cpp` - GBM correctness
- `pnl_tests.cpp` - P&L calculations
- `market_maker_tests.cpp` - Agent strategy
- `strong_type_tests.cpp` - Type system semantics
- `cross_validation_tests.cpp` - Python-C++ consistency

**Python tests** (pytest):
```bash
pytest tests/test_visualize_book.py -v
pytest tests/python/ -v  # Cross-validation infrastructure
```

**Cross-validation harness** (`tools/testing/harness.py`):

The cross-validation harness orchestrates end-to-end testing between C++ and Python:
1. Runs C++ `cross_validation_tests` binary with state export
2. For each test scenario, Python replays deltas and compares state
3. Reports any differences between C++ and Python implementations

```bash
# Run cross-validation manually
python -m tools.testing.harness --build-dir build/debug

# With verbose output
python -m tools.testing.harness --build-dir build/debug --verbose

# Keep output for inspection
python -m tools.testing.harness --build-dir build/debug --keep-output
```

**Test pattern example:**
```cpp
class MatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<MatchingEngine>(InstrumentID{1});
    }
    std::unique_ptr<MatchingEngine> engine;

    OrderRequest make_limit_buy(ClientID client, Quantity qty, Price price) {
        return OrderRequest{
            .client_id = client,
            .quantity = qty,
            .price = price,
            .instrument_id = InstrumentID{1},
            .side = OrderSide::BUY,
            .type = OrderType::LIMIT
        };
    }
};

TEST_F(MatchingEngineTest, LimitBuyOrderAddedToEmptyBook) {
    auto result = engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));
    EXPECT_EQ(result.status, OrderStatus::NEW);
    EXPECT_EQ(result.remaining_quantity, Quantity{100});
}
```

## Adding a New Agent

1. Create header in `include/agents/my_agent.hpp`
2. Define config struct with suffix `Config`
3. Extend `Agent` class
4. Implement `on_wakeup()` (required)
5. Optionally override: `on_trade()`, `on_order_accepted()`, `on_order_rejected()`, `on_order_cancelled()`

```cpp
#pragma once
#include "simulation/agent.hpp"

struct MyAgentConfig {
    // Configuration parameters
};

class MyAgent : public Agent {
public:
    explicit MyAgent(ClientID id, MyAgentConfig config)
        : Agent(id), config_m(config) {}

    void on_wakeup(AgentContext& ctx) override {
        // Main logic: use ctx.submit_order(), ctx.schedule_wakeup(), etc.
    }

private:
    MyAgentConfig config_m;
};
```

## Python Tools

**Order book visualization** (`tools/visualize_book.py`):
```bash
python tools/visualize_book.py output/deltas.csv -i  # Interactive mode
python tools/visualize_book.py output/deltas.csv --at 1000 --plot
```

**Price discovery analysis** (`tools/visualize_timeseries.py`):
```bash
python tools/visualize_timeseries.py output/ --analysis
python tools/visualize_timeseries.py output/ -m spread -m mid -o plot.png
```

**Dependencies:** `sortedcontainers`, `matplotlib`, `numpy`, `pytest`

## Simulation Output Files

Generated in `output_dir/`:

| File | Description |
|------|-------------|
| `deltas.csv` | Order book changes (ADD, FILL, CANCEL, MODIFY) |
| `trades.csv` | All executed trades |
| `pnl.csv` | Periodic P&L snapshots per agent |
| `market_state.csv` | Fair price and best bid/ask over time |
| `metadata.json` | Simulation configuration and agent details |

## Common Pitfalls

1. **Don't use raw integers** - Always use strong types (`Price`, `Quantity`, etc.)
2. **Don't forget `_` suffix** - Private members must have `_` suffix
3. **Don't skip `[[nodiscard]]`** - Add for functions where ignoring return is a bug
4. **Don't create raw pointers** - Use `std::unique_ptr` or store by value
5. **`std::shared_ptr`** - Generally a code smell, a different approach is usually better
6. **Event timestamps matter** - Events at same timestamp ordered by sequence number
7. **Run all build variants** - `./run_all.sh` catches memory issues and UB

## Running a Simulation

```bash
# Build
cmake -S . -B build/debug && cmake --build build/debug

# Run with default config
./build/debug/MarketSimulator

# Run with custom config
./build/debug/MarketSimulator --config my_config.json --output /tmp/output

# Analyze results
python tools/visualize_book.py output/deltas.csv -i
python tools/visualize_timeseries.py output/ --analysis
```

## Dependencies

**C++:**
- `nlohmann/json` v3.11.3 (fetched via CMake)
- Google Test (testing only, fetched via CMake)
- C++23 Standard Library

**Python:**
- `sortedcontainers`
- `matplotlib`
- `numpy`
- `pytest`
