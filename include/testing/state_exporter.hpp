#pragma once

#include "exchange/matching_engine.hpp"
#include "simulation/simulation_engine.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace testing {

/**
 * Exports simulation state to JSON for cross-validation testing.
 *
 * Serializes order books (with full order-level detail in FIFO order)
 * and participant P&L to JSON format that can be compared against
 * Python replay state.
 */
class StateExporter {
public:
    /**
     * Export a single order to JSON.
     *
     * Fields: order_id, client_id, quantity, price, timestamp, instrument_id, side
     */
    static nlohmann::json export_order(const Order& order);

    /**
     * Export an order book to JSON.
     *
     * Structure:
     * {
     *   "bids": [{"price": 999, "orders": [...]}, ...],  // descending price
     *   "asks": [{"price": 1001, "orders": [...]}, ...]  // ascending price
     * }
     *
     * Orders within each price level are in FIFO queue order.
     */
    static nlohmann::json export_order_book(const OrderBook& book);

    /**
     * Export P&L state for a single participant.
     *
     * Fields: long_position, short_position, cash
     */
    static nlohmann::json export_pnl(const PnL& pnl);

    /**
     * Export complete simulation state.
     *
     * Structure:
     * {
     *   "timestamp": <current time>,
     *   "sequence_num": <delta sequence that produced this state>,
     *   "order_books": {
     *     "<instrument_id>": <order book JSON>,
     *     ...
     *   },
     *   "pnl": {
     *     "<client_id>": <pnl JSON>,
     *     ...
     *   }
     * }
     */
    static nlohmann::json export_full_state(const SimulationEngine& engine,
                                            Timestamp timestamp,
                                            EventSequenceNumber seq_num);

    /**
     * Write state to a JSON file.
     *
     * Creates file: <dir>/state_<seq_num>.json
     */
    static void write_state_file(const nlohmann::json& state,
                                 const std::filesystem::path& dir,
                                 EventSequenceNumber seq_num);
};

} // namespace testing
