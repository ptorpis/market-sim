#include "testing/test_harness.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// =============================================================================
// Test Fixture
// =============================================================================

class CrossValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Check for external output directory (for end-to-end validation)
        const char* env_dir = std::getenv("CROSS_VAL_OUTPUT_DIR");
        if (env_dir != nullptr && std::strlen(env_dir) > 0) {
            test_dir_ = fs::path(env_dir) / ("test_" + std::to_string(test_counter_++));
            preserve_output_ = true;
        } else {
            test_dir_ = fs::temp_directory_path() /
                        ("cross_val_test_" + std::to_string(test_counter_++));
            preserve_output_ = false;
        }
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        // Clean up test directory unless preserving for external validation
        if (!preserve_output_ && fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    // Verify that expected files were created
    void verify_output_files() {
        EXPECT_TRUE(fs::exists(test_dir_ / "deltas.csv")) << "deltas.csv not created";
        EXPECT_TRUE(fs::exists(test_dir_ / "trades.csv")) << "trades.csv not created";
        EXPECT_TRUE(fs::exists(test_dir_ / "states")) << "states directory not created";
    }

    // Count state files in output directory
    size_t count_state_files() {
        size_t count = 0;
        for (const auto& entry : fs::directory_iterator(test_dir_ / "states")) {
            if (entry.path().extension() == ".json") {
                ++count;
            }
        }
        return count;
    }

    // Verify that a specific state file exists and contains valid JSON
    void verify_state_file(EventSequenceNumber seq_num) {
        std::ostringstream filename;
        filename << "state_" << std::setfill('0') << std::setw(6) << seq_num.value()
                 << ".json";
        fs::path state_file = test_dir_ / "states" / filename.str();

        EXPECT_TRUE(fs::exists(state_file)) << "State file not found: " << state_file;

        if (fs::exists(state_file)) {
            std::ifstream file(state_file);
            nlohmann::json state;
            EXPECT_NO_THROW(file >> state) << "Invalid JSON in: " << state_file;

            // Verify required fields
            EXPECT_TRUE(state.contains("timestamp"));
            EXPECT_TRUE(state.contains("sequence_num"));
            EXPECT_TRUE(state.contains("order_books"));
            EXPECT_TRUE(state.contains("pnl"));
        }
    }

    fs::path test_dir_;
    bool preserve_output_ = false;
    static inline int test_counter_ = 0;
};

// =============================================================================
// Basic Operations Tests
// =============================================================================

TEST_F(CrossValidationTest, BasicAdd_SingleBuyOrder_StateExported) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    // Schedule a single buy order
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{50},
                           Price{1000}, OrderSide::BUY);

    harness.run_with_state_export(Timestamp{200});

    verify_output_files();

    // Should have at least 2 state files: initial + after order
    EXPECT_GE(count_state_files(), 2u);

    // Verify the order book has the order
    const auto& book = harness.engine().get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1u);
    EXPECT_EQ(book.asks.size(), 0u);
}

TEST_F(CrossValidationTest, BasicAdd_SingleSellOrder_StateExported) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{50},
                           Price{1000}, OrderSide::SELL);

    harness.run_with_state_export(Timestamp{200});

    verify_output_files();

    const auto& book = harness.engine().get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 0u);
    EXPECT_EQ(book.asks.size(), 1u);
}

TEST_F(CrossValidationTest, BasicFill_PartialMatch_StateExported) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    // Buy 100, then sell 50 -> partial fill
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);
    harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{50},
                           Price{1000}, OrderSide::SELL);

    harness.run_with_state_export(Timestamp{300});

    verify_output_files();

    // After partial fill, bid should have 50 remaining
    const auto& book = harness.engine().get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1u);

    auto it = book.bids.begin();
    EXPECT_EQ(it->second.front().quantity.value(), 50u);
}

TEST_F(CrossValidationTest, BasicFill_CompleteMatch_StateExported) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    // Buy 100, then sell 100 -> complete fill
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);
    harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    harness.run_with_state_export(Timestamp{300});

    verify_output_files();

    // After complete fill, book should be empty
    const auto& book = harness.engine().get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 0u);
    EXPECT_EQ(book.asks.size(), 0u);
}

TEST_F(CrossValidationTest, BasicCancel_OrderRemoved_StateExported) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);
    harness.schedule_cancel(Timestamp{200}, ClientID{1}, OrderID{1});

    harness.run_with_state_export(Timestamp{300});

    verify_output_files();

    // After cancel, book should be empty
    const auto& book = harness.engine().get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 0u);
}

// =============================================================================
// FIFO Verification Tests
// =============================================================================

TEST_F(CrossValidationTest, Fifo_SamePriceMultipleOrders_FifoPreserved) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    // Add three buy orders at same price from different clients
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);
    harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);
    harness.schedule_order(Timestamp{300}, ClientID{3}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // Sell 150 -> should fill client 1 fully, client 2 partially
    harness.schedule_order(Timestamp{400}, ClientID{4}, InstrumentID{1}, Quantity{150},
                           Price{1000}, OrderSide::SELL);

    harness.run_with_state_export(Timestamp{500});

    verify_output_files();

    // Check P&L: client 1 should have full position, client 2 partial
    const auto& pnl_1 = harness.engine().get_pnl(ClientID{1});
    const auto& pnl_2 = harness.engine().get_pnl(ClientID{2});
    const auto& pnl_3 = harness.engine().get_pnl(ClientID{3});

    // Client 1 filled 100 (first in queue)
    EXPECT_EQ(pnl_1.long_position.value(), 100u);

    // Client 2 filled 50 (second in queue, partial)
    EXPECT_EQ(pnl_2.long_position.value(), 50u);

    // Client 3 not filled yet (third in queue)
    EXPECT_EQ(pnl_3.long_position.value(), 0u);
}

// =============================================================================
// Self-Trade Prevention Tests
// =============================================================================

TEST_F(CrossValidationTest, SelfTrade_SameClientBothSides_NoMatch) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    // Client 1 has buy order
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // Client 2 has buy order behind client 1
    harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // Client 1 sells -> should skip own order, match with client 2
    harness.schedule_order(Timestamp{300}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    harness.run_with_state_export(Timestamp{400});

    verify_output_files();

    // Client 1's buy order should still be in book (self-trade prevented)
    const auto& book = harness.engine().get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1u);

    // Client 2 should have been filled (matched with client 1's sell)
    const auto& pnl_2 = harness.engine().get_pnl(ClientID{2});
    EXPECT_EQ(pnl_2.long_position.value(), 100u);

    // Client 1 should have short position from selling to client 2
    const auto& pnl_1 = harness.engine().get_pnl(ClientID{1});
    EXPECT_EQ(pnl_1.short_position.value(), 100u);
    // Client 1 should NOT have long position (didn't match own order)
    EXPECT_EQ(pnl_1.long_position.value(), 0u);
}

// =============================================================================
// P&L Conservation Tests
// =============================================================================

TEST_F(CrossValidationTest, Pnl_SingleTrade_CashSumsToZero) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);
    harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    harness.run_with_state_export(Timestamp{300});

    // Cash should sum to zero
    std::int64_t total_cash = 0;
    for (const auto& [client_id, pnl] : harness.engine().all_pnl()) {
        total_cash += pnl.cash.value();
    }
    EXPECT_EQ(total_cash, 0);
}

TEST_F(CrossValidationTest, Pnl_MultipleTrades_CashSumsToZero) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

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

    harness.run_with_state_export(Timestamp{300});

    // Cash should sum to zero
    std::int64_t total_cash = 0;
    for (const auto& [client_id, pnl] : harness.engine().all_pnl()) {
        total_cash += pnl.cash.value();
    }
    EXPECT_EQ(total_cash, 0);
}

TEST_F(CrossValidationTest, Pnl_NetPositionsSumToZero) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    // Multiple trades
    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);
    harness.schedule_order(Timestamp{101}, ClientID{2}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    harness.schedule_order(Timestamp{200}, ClientID{3}, InstrumentID{1}, Quantity{50},
                           Price{999}, OrderSide::BUY);
    harness.schedule_order(Timestamp{201}, ClientID{1}, InstrumentID{1}, Quantity{50},
                           Price{999}, OrderSide::SELL);

    harness.run_with_state_export(Timestamp{300});

    // Net positions should sum to zero
    std::int64_t total_net = 0;
    for (const auto& [client_id, pnl] : harness.engine().all_pnl()) {
        total_net += pnl.net_position();
    }
    EXPECT_EQ(total_net, 0);
}

// =============================================================================
// Modify Operations Tests
// =============================================================================

TEST_F(CrossValidationTest, Modify_QuantityDown_SameOrderId) {
    testing::TestHarness harness;
    harness.add_instrument(InstrumentID{1});
    harness.set_output_directory(test_dir_);

    harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // Modify: reduce quantity to 50 at same price
    harness.schedule_modify(Timestamp{200}, ClientID{1}, OrderID{1}, Quantity{50},
                            Price{1000});

    harness.run_with_state_export(Timestamp{300});

    verify_output_files();

    // Order should have reduced quantity
    const auto& book = harness.engine().get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1u);

    auto it = book.bids.begin();
    EXPECT_EQ(it->second.front().quantity.value(), 50u);
}

// =============================================================================
// Predefined Scenario Tests
// =============================================================================

TEST_F(CrossValidationTest, Scenario_BasicOperations) {
    auto harness = testing::scenarios::basic_operations(test_dir_);
    harness.run_with_state_export(Timestamp{600});

    verify_output_files();
    EXPECT_GE(count_state_files(), 5u);
}

TEST_F(CrossValidationTest, Scenario_FifoVerification) {
    auto harness = testing::scenarios::fifo_verification(test_dir_);
    harness.run_with_state_export(Timestamp{500});

    verify_output_files();
}

TEST_F(CrossValidationTest, Scenario_SelfTradePrevention) {
    auto harness = testing::scenarios::self_trade_prevention(test_dir_);
    harness.run_with_state_export(Timestamp{400});

    verify_output_files();
}

TEST_F(CrossValidationTest, Scenario_ModifyOperations) {
    auto harness = testing::scenarios::modify_operations(test_dir_);
    harness.run_with_state_export(Timestamp{500});

    verify_output_files();
}

TEST_F(CrossValidationTest, Scenario_PnlConservation) {
    auto harness = testing::scenarios::pnl_conservation(test_dir_);
    harness.run_with_state_export(Timestamp{300});

    verify_output_files();

    // Verify cash sums to zero
    std::int64_t total_cash = 0;
    for (const auto& [client_id, pnl] : harness.engine().all_pnl()) {
        total_cash += pnl.cash.value();
    }
    EXPECT_EQ(total_cash, 0);
}

// =============================================================================
// Determinism Tests
// =============================================================================

TEST_F(CrossValidationTest, Determinism_SameInputTwice_IdenticalOutput) {
    // Run scenario twice, verify same final state
    auto run_scenario = []() {
        testing::TestHarness harness;
        harness.add_instrument(InstrumentID{1});
        // Don't set output directory - we just want to check final state

        harness.schedule_order(Timestamp{100}, ClientID{1}, InstrumentID{1},
                               Quantity{100}, Price{1000}, OrderSide::BUY);
        harness.schedule_order(Timestamp{200}, ClientID{2}, InstrumentID{1}, Quantity{50},
                               Price{1000}, OrderSide::SELL);
        harness.schedule_order(Timestamp{300}, ClientID{3}, InstrumentID{1}, Quantity{50},
                               Price{1000}, OrderSide::SELL);

        harness.run(Timestamp{400});

        // Return final state summary
        const auto& book = harness.engine().get_order_book(InstrumentID{1});
        const auto& pnl = harness.engine().all_pnl();

        struct State {
            size_t bid_levels;
            size_t ask_levels;
            std::int64_t total_cash;
        };

        State state{.bid_levels = book.bids.size(),
                    .ask_levels = book.asks.size(),
                    .total_cash = 0};

        for (const auto& [client_id, p] : pnl) {
            state.total_cash += p.cash.value();
        }

        return state;
    };

    auto state1 = run_scenario();
    auto state2 = run_scenario();

    EXPECT_EQ(state1.bid_levels, state2.bid_levels);
    EXPECT_EQ(state1.ask_levels, state2.ask_levels);
    EXPECT_EQ(state1.total_cash, state2.total_cash);
}
