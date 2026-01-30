#pragma once

#include "simulation/simulation_engine.hpp"
#include "testing/state_exporter.hpp"

#include <filesystem>
#include <functional>
#include <set>
#include <vector>

namespace testing {

/**
 * Callback invoked after each state-changing operation.
 *
 * Parameters: sequence_num, timestamp, state_json
 */
using StateExportCallback =
    std::function<void(EventSequenceNumber, Timestamp, const nlohmann::json&)>;

/**
 * Test harness for cross-validation testing.
 *
 * Wraps SimulationEngine and provides:
 * - Deterministic scenario building (schedule orders, cancels, modifies)
 * - State export after each delta for comparison with Python replay
 * - Integration with persistence for delta/trade recording
 *
 * Usage:
 *   TestHarness harness;
 *   harness.add_instrument(InstrumentID{1});
 *   harness.set_output_directory("/tmp/test_output");
 *   harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1},
 *                          Quantity{50}, Price{1000}, OrderSide::BUY);
 *   harness.run_with_state_export(Timestamp{200});
 *   // State files and deltas.csv written to output directory
 */
class TestHarness {
public:
    explicit TestHarness(Timestamp latency = Timestamp{0});

    // Setup
    void add_instrument(InstrumentID id);
    void set_output_directory(const std::filesystem::path& dir);
    void set_state_export_callback(StateExportCallback callback);

    // Deterministic scenario building - schedule events directly
    void schedule_order(Timestamp ts, ClientID client, InstrumentID instrument,
                        Quantity qty, Price price, OrderSide side,
                        OrderType type = OrderType::LIMIT);

    void schedule_cancel(Timestamp ts, ClientID client, OrderID order_id);

    void schedule_modify(Timestamp ts, ClientID client, OrderID order_id,
                         Quantity new_qty, Price new_price);

    /**
     * Run simulation until end_time, exporting state after each event.
     *
     * This processes events one at a time and exports full simulation state
     * after each step. State files are written to:
     *   <output_dir>/states/state_NNNNNN.json
     *
     * Also produces standard persistence output:
     *   <output_dir>/deltas.csv
     *   <output_dir>/trades.csv
     */
    void run_with_state_export(Timestamp end_time);

    /**
     * Run simulation without state export (for comparison/baseline tests).
     */
    void run(Timestamp end_time);

    // Access to underlying engine for assertions
    const SimulationEngine& engine() const { return engine_; }
    SimulationEngine& engine() { return engine_; }

    // Get the set of configured instruments
    const std::set<InstrumentID>& instruments() const { return instruments_; }

private:
    SimulationEngine engine_;
    std::filesystem::path output_dir_;
    std::set<InstrumentID> instruments_;
    StateExportCallback state_callback_;
    EventSequenceNumber state_sequence_{0};

    /**
     * Export current state to JSON file.
     */
    void export_current_state();

    /**
     * Build full state JSON including all order books.
     */
    nlohmann::json build_full_state() const;
};

/**
 * Predefined test scenarios for cross-validation.
 *
 * Each function returns a harness pre-configured with a specific test scenario.
 * Call run_with_state_export() to execute and generate output files.
 */
namespace scenarios {

/**
 * Basic operations: ADD, partial FILL, complete FILL, CANCEL.
 *
 * Sequence:
 * 1. BUY  100 @ 1000 (client 1) -> rests on book
 * 2. SELL  50 @ 1000 (client 2) -> partial fill of order 1
 * 3. SELL  50 @ 1000 (client 3) -> complete fill of order 1
 * 4. BUY  100 @  999 (client 1) -> rests on book
 * 5. CANCEL order from step 4
 */
TestHarness basic_operations(const std::filesystem::path& output_dir);

/**
 * FIFO verification: Multiple orders at same price level.
 *
 * Sequence:
 * 1. BUY 100 @ 1000 (client 1) -> rests, position 1
 * 2. BUY 100 @ 1000 (client 2) -> rests, position 2
 * 3. BUY 100 @ 1000 (client 3) -> rests, position 3
 * 4. SELL 150 @ 1000 (client 4) -> fills client 1 fully, client 2 partially
 *
 * Validates that client 1's order is filled before client 2's.
 */
TestHarness fifo_verification(const std::filesystem::path& output_dir);

/**
 * Self-trade prevention: Same client on both sides.
 *
 * Sequence:
 * 1. BUY 100 @ 1000 (client 1) -> rests
 * 2. BUY 100 @ 1000 (client 2) -> rests
 * 3. SELL 100 @ 1000 (client 1) -> should skip own order, fill client 2
 */
TestHarness self_trade_prevention(const std::filesystem::path& output_dir);

/**
 * Modify operations: quantity down (same ID), price change (new ID).
 *
 * Sequence:
 * 1. BUY 100 @ 1000 (client 1) -> rests
 * 2. MODIFY: reduce qty to 50 -> same order_id
 * 3. MODIFY: change price to 1001 -> new order_id, loses FIFO position
 */
TestHarness modify_operations(const std::filesystem::path& output_dir);

/**
 * PnL conservation: verify cash and positions sum to zero.
 *
 * Sequence:
 * 1. BUY 100 @ 1000 (client 1)
 * 2. SELL 100 @ 1000 (client 2) -> trade at 1000
 * 3. BUY 50 @ 1001 (client 3)
 * 4. SELL 50 @ 1001 (client 1) -> client 1 sells to client 3
 */
TestHarness pnl_conservation(const std::filesystem::path& output_dir);

} // namespace scenarios

} // namespace testing
