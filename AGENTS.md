# AGENTS.md - AI Agent Guide for Market Simulator
This document helps AI agents understand the codebase structure, conventions, and design patterns used in this project.
## Project Overview
A discrete-time, event-driven market simulation framework in C++23. Simulates financial market microstructure with agents, order books, and a matching engine.
## Architecture
### Core Components
1. **SimulationEngine** (`include/simulation/simulation_engine.hpp`)
   - Main orchestrator implementing `AgentContext` interface
   - Manages agents, matching engines, event scheduler
   - Dispatches events via visitor pattern
2. **Scheduler** (`include/simulation/scheduler.hpp`)
   - Priority queue ordering events by (timestamp, sequence_number)
   - Maintains simulation time
3. **MatchingEngine** (`include/exchange/matching_engine.hpp`)
   - Per-instrument order book management
   - Uses dispatch table + policy-based design for order matching
   - Supports limit and market orders
4. **Agent** (`include/agents/agent.hpp`)
   - Abstract base class for trading agents
   - Receives callbacks via `AgentContext` interface
### Data Flow
```
Agent → AgentContext → Scheduler → Event queued
                                        ↓
                          Scheduler pops next event
                                        ↓
                    SimulationEngine dispatches to handler
                                        ↓
                    MatchingEngine processes (if order)
                                        ↓
                    Agent callbacks triggered
```
## Coding Conventions
### Naming
| Element | Convention | Example |
|---------|------------|---------|
| Functions/variables | `snake_case` | `submit_order()`, `order_id` |
| Classes/structs | `PascalCase` | `MatchingEngine`, `OrderBook` |
| Config structs | Suffix with `Config` | `NoiseTraderConfig` |
| Boolean methods | Prefix `is_` or suffix `needs_` | `is_zero()`, `needs_price_check()` |
| Private members | Suffix with `_m` | `data_m`, `config_m` |
| Private/impl functions | Suffix with `_` | `add_to_book_()` |
### Strong Types
The project uses type-safe wrappers defined in `include/utils/types.hpp`:
```cpp
using Timestamp = StrongType<std::uint64_t, struct TimestampTag>;
using Price = StrongType<std::int64_t, struct PriceTag>;
using Quantity = StrongType<std::uint64_t, struct QuantityTag>;
using OrderID = StrongType<std::uint64_t, struct OrderIDTag>;
using ClientID = StrongType<std::uint64_t, struct ClientIDTag>;
// etc.
```

**Always use these types instead of raw integers.** Access underlying value with `.value()`.

### Code Quality Requirements

- Use `constexpr` where possible
- Add `[[nodiscard]]` for important return values
- Use `[[maybe_unused]]` for unused parameters in virtual overrides
- Prefer `std::optional` over sentinel values
- Use `std::variant` and `std::visit` for type-safe unions
- Prefer annotations, or `std::ignore` instead of void cast, avoid C-style cast in general
- Use the same type and minimize casts when doing calculations

## Build System
### Quick Commands
```bash
# Build and test all variants
./run_all.sh
# Manual build
cmake -S . -B build/debug
cmake --build build/debug
ctest --test-dir build/debug
```
### Build Variants
| Variant | Purpose | Flags |
|---------|---------|-------|
| `debug` | Development | `-g -O0` |
| `asan` | Memory safety | AddressSanitizer + UBSan |
| `valgrind` | Memory checking | Valgrind integration |
### Compiler Requirements
- C++23 standard
- Strict warnings: Always keep warnings on, treat warnings as errors, see CMakeLists.txt for all the warnings enabled
## Testing
Uses Google Test framework. Tests located in `tests/`:
- `matching_engine_tests.cpp` - Order matching logic
- `scheduler_tests.cpp` - Event scheduling
- `simulation_engine_tests.cpp` - Integration tests
### Running Tests
```bash
ctest --test-dir build/debug --output-on-failure
```
## Adding New Code
### Adding a New Agent
1. Create header in `include/agents/my_agent.hpp`
2. Extend `Agent` class
3. Implement required callbacks:
```cpp
#pragma once
#include "agent.hpp"
struct MyAgentConfig {
    // Configuration parameters
};
class MyAgent : public Agent {
public:
    explicit MyAgent(ClientID id, MyAgentConfig config)
        : Agent(id), config_m(config) {}

    void on_wakeup(AgentContext& ctx) override {
        // Called when agent's scheduled wakeup fires
        // Use ctx.submit_order(), ctx.schedule_wakeup(), etc.
    }

    // Optional overrides:
    // void on_order_accepted(AgentContext&, const OrderAccepted&) override;
    // void on_order_rejected(AgentContext&, const OrderRejected&) override;
    // void on_trade(AgentContext&, const Trade&) override;

private:
    MyAgentConfig config_m;
};
```

4. Register in simulation (see `src/main.cpp` for examples)

### Adding New Events

1. Define event struct in `include/simulation/events.hpp`
2. Add to `Event` variant type
3. Add `handle()` overload in `SimulationEngine`

## Key Design Patterns

### 1. Policy-Based Design (Matching Engine)

```cpp
template<typename SidePolicy, typename OrderTypePolicy>
std::vector<Trade> match(Order& order);
```

Policies like `BuySide`, `SellSide`, `LimitOrderPolicy`, `MarketOrderPolicy` configure matching behavior.

### 2. Dispatch Table

Function pointer table in `MatchingEngine` maps (OrderType, Side) combinations to handlers.

### 3. Visitor Pattern

Events dispatched via `std::visit`:

```cpp
std::visit([this](const auto& e) { handle(e); }, event);
```

### 4. CRTP for Strong Types

`StrongType<Base, Tag>` uses Curiously Recurring Template Pattern for zero-cost type safety.

## Common Pitfalls

1. **Don't use raw integers** - Use strong types (`Price`, `Quantity`, etc.)
2. **Don't forget `_m` suffix** - Private members must have `_m` suffix
3. **Don't skip `[[nodiscard]]`** - Add for functions where ignoring return is likely a bug
4. **Don't create raw pointers** - Use `std::unique_ptr` or store by value
5. **Event timestamps matter** - Events with same timestamp ordered by sequence number

## Dependencies

- **Google Test** - Testing only
- **Standard Library** - `<variant>`, `<optional>`, `<format>`, `<ranges>`, `<random>`, `<print>`

No other external dependencies.

## Example: Running a Simulation

See `src/main.cpp` for a complete example with:
- Multiple agent types (NoiseTrader, MarketMaker, InformedTrader)
- Geometric Brownian Motion fair price generation
- P&L tracking and reporting

## Key Files to Understand First

1. `include/utils/types.hpp` - Type system foundation
2. `include/exchange/matching_engine.hpp` - Core matching logic
3. `include/simulation/simulation_engine.hpp` - Main orchestrator
4. `include/agents/agent.hpp` - Agent interface
5. `src/main.cpp` - Working example
