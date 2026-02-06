#include "testing/test_harness.hpp"

namespace testing {

TestHarness::TestHarness(Timestamp latency) : engine_(latency) {}

void TestHarness::add_instrument(InstrumentID id) {
    engine_.add_instrument(id);
    instruments_.insert(id);
}

void TestHarness::set_output_directory(const std::filesystem::path& dir) {
    output_dir_ = dir;
    std::filesystem::create_directories(dir);
    std::filesystem::create_directories(dir / "states");
    engine_.enable_persistence(dir);
}

void TestHarness::set_state_export_callback(StateExportCallback callback) {
    state_callback_ = std::move(callback);
}

void TestHarness::schedule_order(Timestamp ts, ClientID client, InstrumentID instrument,
                                 Quantity qty, Price price, OrderSide side,
                                 OrderType type) {
    engine_.scheduler().schedule(OrderSubmitted{.timestamp = ts,
                                                .agent_id = client,
                                                .instrument_id = instrument,
                                                .quantity = qty,
                                                .price = price,
                                                .side = side,
                                                .type = type});
}

void TestHarness::schedule_cancel(Timestamp ts, ClientID client, OrderID order_id) {
    engine_.scheduler().schedule(
        CancellationSubmitted{.timestamp = ts, .agent_id = client, .order_id = order_id});
}

void TestHarness::schedule_modify(Timestamp ts, ClientID client, OrderID order_id,
                                  Quantity new_qty, Price new_price) {
    engine_.scheduler().schedule(ModificationSubmitted{.timestamp = ts,
                                                       .agent_id = client,
                                                       .order_id = order_id,
                                                       .new_quantity = new_qty,
                                                       .new_price = new_price});
}

void TestHarness::run_with_state_export(Timestamp end_time) {
    // Export initial state (empty books)
    export_current_state();

    while (!engine_.scheduler().empty() &&
           get_timestamp(engine_.scheduler().peek()) <= end_time) {
        engine_.step();
        export_current_state();
    }

    engine_.finalize_persistence();
}

void TestHarness::run(Timestamp end_time) {
    engine_.run_until(end_time);
    engine_.finalize_persistence();
}

void TestHarness::export_current_state() {
    nlohmann::json state = build_full_state();

    // Write to file
    if (!output_dir_.empty()) {
        StateExporter::write_state_file(state, output_dir_ / "states", state_sequence_);
    }

    // Invoke callback if set
    if (state_callback_) {
        state_callback_(state_sequence_, engine_.now(), state);
    }

    ++state_sequence_;
}

nlohmann::json TestHarness::build_full_state() const {
    nlohmann::json state;
    state["timestamp"] = engine_.now().value();
    state["sequence_num"] = state_sequence_.value();

    // Export order books for all configured instruments
    nlohmann::json order_books = nlohmann::json::object();
    for (const auto& instrument_id : instruments_) {
        const OrderBook& book = engine_.get_order_book(instrument_id);
        order_books[std::to_string(instrument_id.value())] =
            StateExporter::export_order_book(book);
    }
    state["order_books"] = order_books;

    // Export all P&L
    nlohmann::json pnl_map = nlohmann::json::object();
    for (const auto& [client_id, pnl] : engine_.all_pnl()) {
        pnl_map[std::to_string(client_id.value())] = StateExporter::export_pnl(pnl);
    }
    state["pnl"] = pnl_map;

    return state;
}

// =============================================================================
// Predefined Test Scenarios
// =============================================================================

namespace scenarios {

TestHarness basic_operations(const std::filesystem::path& output_dir) {
    TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(output_dir);

    // 1. BUY 100 @ 1000 (client 1) -> rests on book
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // 2. SELL 50 @ 1000 (client 2) -> partial fill of order 1
    harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{50},
                           Price{1000}, OrderSide::SELL);

    // 3. SELL 50 @ 1000 (client 3) -> complete fill of order 1
    harness.schedule_order(Timestamp{300}, ClientID{3}, InstrumentID{1}, Quantity{50},
                           Price{1000}, OrderSide::SELL);

    // 4. BUY 100 @ 999 (client 1) -> rests on book
    harness.schedule_order(Timestamp{400}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{999}, OrderSide::BUY);

    // 5. CANCEL order from step 4 (order_id will be 4 after the trades)
    harness.schedule_cancel(Timestamp{500}, ClientID{1}, OrderID{4});

    return harness;
}

TestHarness fifo_verification(const std::filesystem::path& output_dir) {
    TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(output_dir);

    // Three buy orders at the same price - test FIFO order
    // 1. BUY 100 @ 1000 (client 1) -> position 1 in queue
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // 2. BUY 100 @ 1000 (client 2) -> position 2 in queue
    harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // 3. BUY 100 @ 1000 (client 3) -> position 3 in queue
    harness.schedule_order(Timestamp{300}, ClientID{3}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // 4. SELL 150 @ 1000 (client 4) -> should fill client 1 fully, client 2 partially
    harness.schedule_order(Timestamp{400}, ClientID{4}, InstrumentID{1}, Quantity{150},
                           Price{1000}, OrderSide::SELL);

    return harness;
}

TestHarness self_trade_prevention(const std::filesystem::path& output_dir) {
    TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(output_dir);

    // 1. BUY 100 @ 1000 (client 1) -> rests
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // 2. BUY 100 @ 1000 (client 2) -> rests behind client 1
    harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // 3. SELL 100 @ 1000 (client 1) -> should skip own order, fill client 2's order
    harness.schedule_order(Timestamp{300}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    return harness;
}

TestHarness modify_operations(const std::filesystem::path& output_dir) {
    TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(output_dir);

    // 1. BUY 100 @ 1000 (client 1) -> rests, order_id = 1
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // 2. MODIFY: reduce qty to 50 -> same order_id
    harness.schedule_modify(Timestamp{200}, ClientID{1}, OrderID{1}, Quantity{50},
                            Price{1000});

    // 3. Add another order to test FIFO position
    harness.schedule_order(Timestamp{300}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // 4. MODIFY: change price to 1001 -> new order_id, should be at back of 1001 queue
    //    But since there's no other order at 1001, it'll be alone there
    harness.schedule_modify(Timestamp{400}, ClientID{1}, OrderID{1}, Quantity{50},
                            Price{1001});

    return harness;
}

TestHarness pnl_conservation(const std::filesystem::path& output_dir) {
    TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(output_dir);

    // Trade 1: Client 1 buys from Client 2 at 1000
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);
    harness.schedule_order(Timestamp{101}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    // Trade 2: Client 3 buys from Client 1 at 1001
    harness.schedule_order(Timestamp{200}, ClientID{3}, InstrumentID{1}, Quantity{50},
                           Price{1001}, OrderSide::BUY);
    harness.schedule_order(Timestamp{201}, ClientID{1}, InstrumentID{1}, Quantity{50},
                           Price{1001}, OrderSide::SELL);

    return harness;
}

} // namespace scenarios

} // namespace testing
