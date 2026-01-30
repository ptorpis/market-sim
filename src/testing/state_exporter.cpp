#include "testing/state_exporter.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace testing {

nlohmann::json StateExporter::export_order(const Order& order) {
    return nlohmann::json{
        {"order_id", order.order_id.value()},
        {"client_id", order.client_id.value()},
        {"quantity", order.quantity.value()},
        {"price", order.price.value()},
        {"timestamp", order.timestamp.value()},
        {"instrument_id", order.instrument_id.value()},
        {"side", order.side == OrderSide::BUY ? "BUY" : "SELL"}};
}

nlohmann::json StateExporter::export_order_book(const OrderBook& book) {
    nlohmann::json result;

    // Export bids - map is sorted descending (std::greater<Price>)
    nlohmann::json bids = nlohmann::json::array();
    for (const auto& [price, queue] : book.bids) {
        nlohmann::json level;
        level["price"] = price.value();
        nlohmann::json orders = nlohmann::json::array();
        // Iterate deque front to back (FIFO order)
        for (const Order& order : queue) {
            orders.push_back(export_order(order));
        }
        level["orders"] = orders;
        bids.push_back(level);
    }
    result["bids"] = bids;

    // Export asks - map is sorted ascending (std::less<Price>)
    nlohmann::json asks = nlohmann::json::array();
    for (const auto& [price, queue] : book.asks) {
        nlohmann::json level;
        level["price"] = price.value();
        nlohmann::json orders = nlohmann::json::array();
        // Iterate deque front to back (FIFO order)
        for (const Order& order : queue) {
            orders.push_back(export_order(order));
        }
        level["orders"] = orders;
        asks.push_back(level);
    }
    result["asks"] = asks;

    return result;
}

nlohmann::json StateExporter::export_pnl(const PnL& pnl) {
    return nlohmann::json{{"long_position", pnl.long_position.value()},
                          {"short_position", pnl.short_position.value()},
                          {"cash", pnl.cash.value()}};
}

nlohmann::json StateExporter::export_full_state(const SimulationEngine& engine,
                                                Timestamp timestamp,
                                                EventSequenceNumber seq_num) {
    nlohmann::json state;
    state["timestamp"] = timestamp.value();
    state["sequence_num"] = seq_num.value();

    // Note: We need to know which instruments exist. The SimulationEngine
    // doesn't expose the full list, so we rely on the test harness to track them.
    // This implementation exports an empty order_books object that the harness
    // will populate by calling export_order_book for each known instrument.
    state["order_books"] = nlohmann::json::object();

    // Export all P&L
    nlohmann::json pnl_map = nlohmann::json::object();
    for (const auto& [client_id, pnl] : engine.all_pnl()) {
        pnl_map[std::to_string(client_id.value())] = export_pnl(pnl);
    }
    state["pnl"] = pnl_map;

    return state;
}

void StateExporter::write_state_file(const nlohmann::json& state,
                                     const std::filesystem::path& dir,
                                     EventSequenceNumber seq_num) {
    std::ostringstream filename;
    filename << "state_" << std::setfill('0') << std::setw(6) << seq_num.value()
             << ".json";

    std::filesystem::path filepath = dir / filename.str();
    std::ofstream out(filepath);
    out << state.dump(2);
}

} // namespace testing
