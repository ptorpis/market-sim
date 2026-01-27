#include <gtest/gtest.h>

#include "persistence/csv_writer.hpp"
#include "persistence/data_collector.hpp"
#include "persistence/metadata_writer.hpp"
#include "persistence/records.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// =============================================================================
// Test Helpers
// =============================================================================

inline Order make_test_order(OrderID order_id = OrderID{1},
                             ClientID client_id = ClientID{1},
                             Quantity quantity = Quantity{50}, Price price = Price{1000},
                             Timestamp timestamp = Timestamp{100},
                             InstrumentID instrument_id = InstrumentID{1},
                             OrderSide side = OrderSide::BUY,
                             OrderType type = OrderType::LIMIT,
                             OrderStatus status = OrderStatus::NEW) {
    return Order{.order_id = order_id,
                 .client_id = client_id,
                 .quantity = quantity,
                 .price = price,
                 .timestamp = timestamp,
                 .instrument_id = instrument_id,
                 .side = side,
                 .type = type,
                 .status = status};
}

// =============================================================================
// Test Fixture with Temporary Directory
// =============================================================================

class PersistenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique temp directory for each test
        test_dir_ = fs::temp_directory_path() /
                    ("market_sim_test_" + std::to_string(test_counter_++));
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        // Clean up temp directory
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    fs::path test_dir_;
    static inline int test_counter_ = 0;

    // Helper to read file contents
    std::string read_file(const fs::path& path) {
        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Helper to count lines in a file (excluding header)
    size_t count_data_lines(const fs::path& path) {
        std::ifstream file(path);
        size_t count = 0;
        std::string line;
        while (std::getline(file, line)) {
            ++count;
        }
        return count > 0 ? count - 1 : 0; // Subtract header line
    }
};

// =============================================================================
// Records Tests
// =============================================================================

TEST(RecordsTest, DeltaTypeToString) {
    EXPECT_STREQ(delta_type_to_string(DeltaType::ADD), "ADD");
    EXPECT_STREQ(delta_type_to_string(DeltaType::FILL), "FILL");
    EXPECT_STREQ(delta_type_to_string(DeltaType::CANCEL), "CANCEL");
    EXPECT_STREQ(delta_type_to_string(DeltaType::MODIFY), "MODIFY");
}

TEST(RecordsTest, OrderSideToString) {
    EXPECT_STREQ(order_side_to_string(OrderSide::BUY), "BUY");
    EXPECT_STREQ(order_side_to_string(OrderSide::SELL), "SELL");
}

TEST(RecordsTest, OrderDeltaDefaultValues) {
    OrderDelta delta{.timestamp = Timestamp{100},
                     .sequence_num = EventSequenceNumber{1},
                     .type = DeltaType::ADD,
                     .order_id = OrderID{1},
                     .client_id = ClientID{1},
                     .instrument_id = InstrumentID{1},
                     .side = OrderSide::BUY,
                     .price = Price{1000},
                     .quantity = Quantity{50},
                     .remaining_qty = Quantity{50}};

    // Optional fields should default to 0
    EXPECT_EQ(delta.trade_id.value(), 0);
    EXPECT_EQ(delta.new_order_id.value(), 0);
    EXPECT_EQ(delta.new_price.value(), 0);
    EXPECT_EQ(delta.new_quantity.value(), 0);
}

// =============================================================================
// CSVWriter Tests
// =============================================================================

TEST_F(PersistenceTest, CSVWriterCreatesFiles) {
    { CSVWriter writer(test_dir_); } // Destructor called here

    EXPECT_TRUE(fs::exists(test_dir_ / "deltas.csv"));
    EXPECT_TRUE(fs::exists(test_dir_ / "trades.csv"));
    EXPECT_TRUE(fs::exists(test_dir_ / "pnl.csv"));
}

TEST_F(PersistenceTest, CSVWriterWritesHeaders) {
    { CSVWriter writer(test_dir_); }

    std::string deltas_content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(deltas_content.find("timestamp,sequence_num,delta_type") !=
                std::string::npos);

    std::string trades_content = read_file(test_dir_ / "trades.csv");
    EXPECT_TRUE(trades_content.find("timestamp,trade_id,instrument_id") !=
                std::string::npos);

    std::string pnl_content = read_file(test_dir_ / "pnl.csv");
    EXPECT_TRUE(pnl_content.find("timestamp,client_id,long_position") !=
                std::string::npos);
}

TEST_F(PersistenceTest, CSVWriterWritesDelta) {
    {
        CSVWriter writer(test_dir_);
        writer.write_delta(OrderDelta{.timestamp = Timestamp{100},
                                      .sequence_num = EventSequenceNumber{1},
                                      .type = DeltaType::ADD,
                                      .order_id = OrderID{42},
                                      .client_id = ClientID{5},
                                      .instrument_id = InstrumentID{1},
                                      .side = OrderSide::BUY,
                                      .price = Price{1000},
                                      .quantity = Quantity{50},
                                      .remaining_qty = Quantity{50}});
    }

    std::string content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(content.find("100,1,ADD,42,5,1,BUY,1000,50,50") != std::string::npos);
}

TEST_F(PersistenceTest, CSVWriterWritesTrade) {
    {
        CSVWriter writer(test_dir_);
        writer.write_trade(TradeRecord{.timestamp = Timestamp{200},
                                       .trade_id = TradeID{1},
                                       .instrument_id = InstrumentID{1},
                                       .buyer_id = ClientID{1},
                                       .seller_id = ClientID{2},
                                       .buyer_order_id = OrderID{10},
                                       .seller_order_id = OrderID{20},
                                       .price = Price{1000},
                                       .quantity = Quantity{50}});
    }

    std::string content = read_file(test_dir_ / "trades.csv");
    EXPECT_TRUE(content.find("200,1,1,1,2,10,20,1000,50") != std::string::npos);
}

TEST_F(PersistenceTest, CSVWriterWritesPnL) {
    {
        CSVWriter writer(test_dir_);
        writer.write_pnl(PnLSnapshot{.timestamp = Timestamp{1000},
                                     .client_id = ClientID{1},
                                     .long_position = Quantity{100},
                                     .short_position = Quantity{0},
                                     .cash = Cash{-100000},
                                     .fair_price = Price{1000}});
    }

    std::string content = read_file(test_dir_ / "pnl.csv");
    EXPECT_TRUE(content.find("1000,1,100,0,-100000,1000") != std::string::npos);
}

TEST_F(PersistenceTest, CSVWriterWritesMultipleRecords) {
    {
        CSVWriter writer(test_dir_);
        for (int i = 0; i < 100; ++i) {
            writer.write_delta(
                OrderDelta{.timestamp = Timestamp{static_cast<uint64_t>(i * 10)},
                           .sequence_num = EventSequenceNumber{static_cast<uint64_t>(i)},
                           .type = DeltaType::ADD,
                           .order_id = OrderID{static_cast<uint64_t>(i)},
                           .client_id = ClientID{1},
                           .instrument_id = InstrumentID{1},
                           .side = OrderSide::BUY,
                           .price = Price{1000},
                           .quantity = Quantity{50},
                           .remaining_qty = Quantity{50}});
        }
    }

    EXPECT_EQ(count_data_lines(test_dir_ / "deltas.csv"), 100);
}

TEST_F(PersistenceTest, CSVWriterFlushWorks) {
    CSVWriter writer(test_dir_);
    writer.write_delta(OrderDelta{.timestamp = Timestamp{100},
                                  .sequence_num = EventSequenceNumber{1},
                                  .type = DeltaType::ADD,
                                  .order_id = OrderID{1},
                                  .client_id = ClientID{1},
                                  .instrument_id = InstrumentID{1},
                                  .side = OrderSide::BUY,
                                  .price = Price{1000},
                                  .quantity = Quantity{50},
                                  .remaining_qty = Quantity{50}});

    writer.flush();

    // File should be readable while writer is still alive
    std::string content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(content.find("100,1,ADD") != std::string::npos);
}

TEST_F(PersistenceTest, CSVWriterThrowsOnInvalidDirectory) {
    fs::path invalid_dir = "/nonexistent/path/that/does/not/exist";
    EXPECT_THROW(CSVWriter writer(invalid_dir), std::runtime_error);
}

TEST_F(PersistenceTest, CSVWriterWritesFillDelta) {
    {
        CSVWriter writer(test_dir_);
        writer.write_delta(OrderDelta{.timestamp = Timestamp{100},
                                      .sequence_num = EventSequenceNumber{1},
                                      .type = DeltaType::FILL,
                                      .order_id = OrderID{42},
                                      .client_id = ClientID{5},
                                      .instrument_id = InstrumentID{1},
                                      .side = OrderSide::BUY,
                                      .price = Price{1000},
                                      .quantity = Quantity{30},
                                      .remaining_qty = Quantity{20},
                                      .trade_id = TradeID{99}});
    }

    std::string content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(content.find("FILL") != std::string::npos);
    EXPECT_TRUE(content.find("99") != std::string::npos); // trade_id
}

TEST_F(PersistenceTest, CSVWriterWritesModifyDelta) {
    {
        CSVWriter writer(test_dir_);
        writer.write_delta(OrderDelta{.timestamp = Timestamp{100},
                                      .sequence_num = EventSequenceNumber{1},
                                      .type = DeltaType::MODIFY,
                                      .order_id = OrderID{42},
                                      .client_id = ClientID{5},
                                      .instrument_id = InstrumentID{1},
                                      .side = OrderSide::BUY,
                                      .price = Price{1000},
                                      .quantity = Quantity{50},
                                      .remaining_qty = Quantity{30},
                                      .new_order_id = OrderID{43},
                                      .new_price = Price{1010},
                                      .new_quantity = Quantity{30}});
    }

    std::string content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(content.find("MODIFY") != std::string::npos);
    EXPECT_TRUE(content.find("43") != std::string::npos);   // new_order_id
    EXPECT_TRUE(content.find("1010") != std::string::npos); // new_price
}

// =============================================================================
// MetadataWriter Tests
// =============================================================================

TEST_F(PersistenceTest, MetadataWriterCreatesFile) {
    MetadataWriter metadata;
    metadata.set_simulation_config(Timestamp{10});
    metadata.write(test_dir_);

    EXPECT_TRUE(fs::exists(test_dir_ / "metadata.json"));
}

TEST_F(PersistenceTest, MetadataWriterWritesSimulationConfig) {
    MetadataWriter metadata;
    metadata.set_simulation_config(Timestamp{50});
    metadata.set_duration(Timestamp{100000});
    metadata.write(test_dir_);

    std::string content = read_file(test_dir_ / "metadata.json");
    EXPECT_TRUE(content.find("\"latency\": 50") != std::string::npos);
    EXPECT_TRUE(content.find("\"duration\": 100000") != std::string::npos);
}

TEST_F(PersistenceTest, MetadataWriterWritesInstruments) {
    MetadataWriter metadata;
    metadata.set_simulation_config(Timestamp{0});
    metadata.add_instrument(InstrumentID{1});
    metadata.add_instrument(InstrumentID{2});
    metadata.write(test_dir_);

    std::string content = read_file(test_dir_ / "metadata.json");
    EXPECT_TRUE(content.find("\"instruments\"") != std::string::npos);
    EXPECT_TRUE(content.find("1") != std::string::npos);
    EXPECT_TRUE(content.find("2") != std::string::npos);
}

TEST_F(PersistenceTest, MetadataWriterWritesFairPrice) {
    MetadataWriter metadata;
    metadata.set_simulation_config(Timestamp{0});
    metadata.set_fair_price(FairPriceConfig{.initial_price = Price{1000000},
                                            .drift = 0.0,
                                            .volatility = 0.005,
                                            .tick_size = Timestamp{1000}},
                            42);
    metadata.write(test_dir_);

    std::string content = read_file(test_dir_ / "metadata.json");
    EXPECT_TRUE(content.find("\"fair_price\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"initial_price\": 1000000") != std::string::npos);
    EXPECT_TRUE(content.find("\"seed\": 42") != std::string::npos);
}

TEST_F(PersistenceTest, MetadataWriterWritesAgents) {
    MetadataWriter metadata;
    metadata.set_simulation_config(Timestamp{0});

    nlohmann::json config = {{"instrument", 1}, {"some_param", 100}};
    metadata.add_agent(ClientID{1}, "TestAgent", config, 123);
    metadata.write(test_dir_);

    std::string content = read_file(test_dir_ / "metadata.json");
    EXPECT_TRUE(content.find("\"agents\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"client_id\": 1") != std::string::npos);
    EXPECT_TRUE(content.find("\"type\": \"TestAgent\"") != std::string::npos);
    EXPECT_TRUE(content.find("\"seed\": 123") != std::string::npos);
}

TEST_F(PersistenceTest, MetadataWriterWritesMultipleAgents) {
    MetadataWriter metadata;
    metadata.set_simulation_config(Timestamp{0});

    for (int i = 1; i <= 5; ++i) {
        nlohmann::json config = {{"id", i}};
        metadata.add_agent(ClientID{static_cast<uint64_t>(i)}, "Agent", config,
                           static_cast<uint64_t>(i * 100));
    }
    metadata.write(test_dir_);

    std::string content = read_file(test_dir_ / "metadata.json");
    EXPECT_TRUE(content.find("\"client_id\": 1") != std::string::npos);
    EXPECT_TRUE(content.find("\"client_id\": 5") != std::string::npos);
}

// =============================================================================
// DataCollector Tests
// =============================================================================

TEST_F(PersistenceTest, DataCollectorCreatesAllFiles) {
    {
        DataCollector collector(test_dir_);
        collector.finalize(Timestamp{1000});
    }

    EXPECT_TRUE(fs::exists(test_dir_ / "deltas.csv"));
    EXPECT_TRUE(fs::exists(test_dir_ / "trades.csv"));
    EXPECT_TRUE(fs::exists(test_dir_ / "pnl.csv"));
    EXPECT_TRUE(fs::exists(test_dir_ / "metadata.json"));
}

TEST_F(PersistenceTest, DataCollectorRecordsOrderAccepted) {
    {
        DataCollector collector(test_dir_);

        OrderAccepted event{.timestamp = Timestamp{100},
                            .order_id = OrderID{1},
                            .agent_id = ClientID{5},
                            .instrument_id = InstrumentID{1}};

        Order order = make_test_order(OrderID{1}, ClientID{5});

        collector.on_order_accepted(event, order);
        collector.finalize(Timestamp{1000});
    }

    std::string content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(content.find("ADD") != std::string::npos);
    EXPECT_EQ(count_data_lines(test_dir_ / "deltas.csv"), 1);
}

TEST_F(PersistenceTest, DataCollectorRecordsTrade) {
    {
        DataCollector collector(test_dir_);

        Trade trade{.timestamp = Timestamp{200},
                    .trade_id = TradeID{1},
                    .instrument_id = InstrumentID{1},
                    .buyer_order_id = OrderID{10},
                    .seller_order_id = OrderID{20},
                    .buyer_id = ClientID{1},
                    .seller_id = ClientID{2},
                    .quantity = Quantity{50},
                    .price = Price{1000}};

        collector.on_trade(trade);
        collector.finalize(Timestamp{1000});
    }

    EXPECT_EQ(count_data_lines(test_dir_ / "trades.csv"), 1);
}

TEST_F(PersistenceTest, DataCollectorRecordsFill) {
    {
        DataCollector collector(test_dir_);

        Trade trade{.timestamp = Timestamp{200},
                    .trade_id = TradeID{1},
                    .instrument_id = InstrumentID{1},
                    .buyer_order_id = OrderID{10},
                    .seller_order_id = OrderID{20},
                    .buyer_id = ClientID{1},
                    .seller_id = ClientID{2},
                    .quantity = Quantity{50},
                    .price = Price{1000}};

        collector.on_fill(trade, OrderID{10}, ClientID{1}, Quantity{0}, OrderSide::BUY);
        collector.finalize(Timestamp{1000});
    }

    std::string content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(content.find("FILL") != std::string::npos);
}

TEST_F(PersistenceTest, DataCollectorRecordsCancellation) {
    {
        DataCollector collector(test_dir_);

        OrderCancelled event{.timestamp = Timestamp{100},
                             .order_id = OrderID{1},
                             .agent_id = ClientID{5},
                             .remaining_quantity = Quantity{50}};

        Order order = make_test_order(OrderID{1}, ClientID{5}, Quantity{50}, Price{1000},
                                      Timestamp{50});

        collector.on_order_cancelled(event, order);
        collector.finalize(Timestamp{1000});
    }

    std::string content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(content.find("CANCEL") != std::string::npos);
}

TEST_F(PersistenceTest, DataCollectorRecordsModification) {
    {
        DataCollector collector(test_dir_);

        OrderModified event{.timestamp = Timestamp{100},
                            .old_order_id = OrderID{1},
                            .new_order_id = OrderID{2},
                            .agent_id = ClientID{5},
                            .old_price = Price{1000},
                            .new_price = Price{1010},
                            .old_quantity = Quantity{50},
                            .new_quantity = Quantity{30}};

        collector.on_order_modified(event, InstrumentID{1}, OrderSide::BUY);
        collector.finalize(Timestamp{1000});
    }

    std::string content = read_file(test_dir_ / "deltas.csv");
    EXPECT_TRUE(content.find("MODIFY") != std::string::npos);
}

TEST_F(PersistenceTest, DataCollectorSequenceNumbers) {
    {
        DataCollector collector(test_dir_);

        Order order = make_test_order();

        // Add multiple events - each should get unique sequence number
        for (int i = 0; i < 5; ++i) {
            OrderAccepted event{
                .timestamp = Timestamp{static_cast<uint64_t>(100 + i * 10)},
                .order_id = OrderID{static_cast<uint64_t>(i + 1)},
                .agent_id = ClientID{1},
                .instrument_id = InstrumentID{1}};
            order.order_id = OrderID{static_cast<uint64_t>(i + 1)};
            collector.on_order_accepted(event, order);
        }
        collector.finalize(Timestamp{1000});
    }

    std::string content = read_file(test_dir_ / "deltas.csv");
    // Check sequence numbers 0 through 4 are present
    EXPECT_TRUE(content.find(",0,") != std::string::npos);
    EXPECT_TRUE(content.find(",1,") != std::string::npos);
    EXPECT_TRUE(content.find(",2,") != std::string::npos);
    EXPECT_TRUE(content.find(",3,") != std::string::npos);
    EXPECT_TRUE(content.find(",4,") != std::string::npos);
}

// =============================================================================
// P&L Snapshot Tests
// =============================================================================

// Simple PnL struct for testing (matches the one in simulation_engine.hpp)
struct TestPnL {
    Quantity long_position{0};
    Quantity short_position{0};
    Cash cash{0};
};

TEST_F(PersistenceTest, DataCollectorPnLSnapshotRespectInterval) {
    {
        DataCollector collector(test_dir_, Timestamp{1000});

        std::unordered_map<ClientID, TestPnL, strong_hash<ClientID>> pnls;
        pnls[ClientID{1}] = TestPnL{Quantity{100}, Quantity{0}, Cash{-100000}};

        // First snapshot at t=0 should be skipped (interval starts at 0)
        collector.maybe_snapshot_pnl(Timestamp{0}, pnls, Price{1000});

        // t=500 - within interval, should be skipped
        collector.maybe_snapshot_pnl(Timestamp{500}, pnls, Price{1000});

        // t=1000 - should trigger snapshot
        collector.maybe_snapshot_pnl(Timestamp{1000}, pnls, Price{1000});

        // t=1500 - within interval from last snapshot, should be skipped
        collector.maybe_snapshot_pnl(Timestamp{1500}, pnls, Price{1000});

        // t=2000 - should trigger another snapshot
        collector.maybe_snapshot_pnl(Timestamp{2000}, pnls, Price{1000});

        collector.finalize(Timestamp{2000});
    }

    // Should have exactly 2 data lines (two snapshots)
    EXPECT_EQ(count_data_lines(test_dir_ / "pnl.csv"), 2);
}

TEST_F(PersistenceTest, DataCollectorPnLSnapshotMultipleClients) {
    {
        DataCollector collector(test_dir_, Timestamp{100});

        std::unordered_map<ClientID, TestPnL, strong_hash<ClientID>> pnls;
        pnls[ClientID{1}] = TestPnL{Quantity{100}, Quantity{0}, Cash{-100000}};
        pnls[ClientID{2}] = TestPnL{Quantity{0}, Quantity{100}, Cash{100000}};
        pnls[ClientID{3}] = TestPnL{Quantity{50}, Quantity{50}, Cash{0}};

        collector.maybe_snapshot_pnl(Timestamp{100}, pnls, Price{1000});
        collector.finalize(Timestamp{100});
    }

    // Should have 3 data lines (one per client)
    EXPECT_EQ(count_data_lines(test_dir_ / "pnl.csv"), 3);

    std::string content = read_file(test_dir_ / "pnl.csv");
    EXPECT_TRUE(content.find("-100000") != std::string::npos);
    EXPECT_TRUE(content.find("100000") != std::string::npos);
}

// =============================================================================
// Metadata Access Tests
// =============================================================================

TEST_F(PersistenceTest, DataCollectorMetadataAccess) {
    DataCollector collector(test_dir_);

    // Should be able to access and modify metadata
    collector.metadata().set_simulation_config(Timestamp{10});
    collector.metadata().add_instrument(InstrumentID{1});
    collector.finalize(Timestamp{1000});

    std::string content = read_file(test_dir_ / "metadata.json");
    EXPECT_TRUE(content.find("\"latency\": 10") != std::string::npos);
}

// =============================================================================
// JSON Serialization Tests (to_json functions)
// =============================================================================

TEST_F(PersistenceTest, NoiseTraderConfigToJson) {
    NoiseTraderConfig config{.instrument = InstrumentID{1},
                             .fair_value = Price{1000000},
                             .spread = Price{36},
                             .min_quantity = Quantity{10},
                             .max_quantity = Quantity{100},
                             .min_interval = Timestamp{50},
                             .max_interval = Timestamp{200}};

    nlohmann::json j = to_json(config);
    EXPECT_EQ(j["instrument"], 1);
    EXPECT_EQ(j["fair_value"], 1000000);
    EXPECT_EQ(j["spread"], 36);
    EXPECT_EQ(j["min_quantity"], 10);
    EXPECT_EQ(j["max_quantity"], 100);
    EXPECT_EQ(j["min_interval"], 50);
    EXPECT_EQ(j["max_interval"], 200);
}

TEST_F(PersistenceTest, MarketMakerConfigToJson) {
    MarketMakerConfig config{.instrument = InstrumentID{1},
                             .half_spread = Price{5},
                             .quote_size = Quantity{50},
                             .update_interval = Timestamp{100},
                             .inventory_skew_factor = 0.5,
                             .max_position = Quantity{500}};

    nlohmann::json j = to_json(config);
    EXPECT_EQ(j["instrument"], 1);
    EXPECT_EQ(j["half_spread"], 5);
    EXPECT_EQ(j["quote_size"], 50);
    EXPECT_EQ(j["update_interval"], 100);
    EXPECT_DOUBLE_EQ(j["inventory_skew_factor"], 0.5);
    EXPECT_EQ(j["max_position"], 500);
}

TEST_F(PersistenceTest, InformedTraderConfigToJson) {
    InformedTraderConfig config{.instrument = InstrumentID{1},
                                .min_quantity = Quantity{20},
                                .max_quantity = Quantity{80},
                                .min_interval = Timestamp{100},
                                .max_interval = Timestamp{500},
                                .min_edge = Price{3},
                                .observation_noise = 5.0};

    nlohmann::json j = to_json(config);
    EXPECT_EQ(j["instrument"], 1);
    EXPECT_EQ(j["min_quantity"], 20);
    EXPECT_EQ(j["max_quantity"], 80);
    EXPECT_EQ(j["min_interval"], 100);
    EXPECT_EQ(j["max_interval"], 500);
    EXPECT_EQ(j["min_edge"], 3);
    EXPECT_DOUBLE_EQ(j["observation_noise"], 5.0);
}

TEST_F(PersistenceTest, FairPriceConfigToJson) {
    FairPriceConfig config{.initial_price = Price{1000000},
                           .drift = 0.001,
                           .volatility = 0.005,
                           .tick_size = Timestamp{1000}};

    nlohmann::json j = to_json(config);
    EXPECT_EQ(j["initial_price"], 1000000);
    EXPECT_DOUBLE_EQ(j["drift"], 0.001);
    EXPECT_DOUBLE_EQ(j["volatility"], 0.005);
    EXPECT_EQ(j["tick_size"], 1000);
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

TEST_F(PersistenceTest, CSVWriterHandlesLargeValues) {
    {
        CSVWriter writer(test_dir_);
        writer.write_trade(TradeRecord{.timestamp = Timestamp{9999999999999},
                                       .trade_id = TradeID{999999999},
                                       .instrument_id = InstrumentID{1},
                                       .buyer_id = ClientID{1},
                                       .seller_id = ClientID{2},
                                       .buyer_order_id = OrderID{1},
                                       .seller_order_id = OrderID{2},
                                       .price = Price{999999999999},
                                       .quantity = Quantity{999999999}});
    }

    std::string content = read_file(test_dir_ / "trades.csv");
    EXPECT_TRUE(content.find("9999999999999") != std::string::npos);
    EXPECT_TRUE(content.find("999999999999") != std::string::npos);
}

TEST_F(PersistenceTest, CSVWriterHandlesNegativeCash) {
    {
        CSVWriter writer(test_dir_);
        writer.write_pnl(PnLSnapshot{.timestamp = Timestamp{1000},
                                     .client_id = ClientID{1},
                                     .long_position = Quantity{100},
                                     .short_position = Quantity{0},
                                     .cash = Cash{-50000000000LL}, // Large negative
                                     .fair_price = Price{1000}});
    }

    std::string content = read_file(test_dir_ / "pnl.csv");
    EXPECT_TRUE(content.find("-50000000000") != std::string::npos);
}

TEST_F(PersistenceTest, DataCollectorEmptyPnLMap) {
    {
        DataCollector collector(test_dir_, Timestamp{100});
        std::unordered_map<ClientID, TestPnL, strong_hash<ClientID>> empty_pnls;
        collector.maybe_snapshot_pnl(Timestamp{100}, empty_pnls, Price{1000});
        collector.finalize(Timestamp{100});
    }

    // Should have 0 data lines (empty map)
    EXPECT_EQ(count_data_lines(test_dir_ / "pnl.csv"), 0);
}

TEST_F(PersistenceTest, MetadataWriterEmptyAgents) {
    MetadataWriter metadata;
    metadata.set_simulation_config(Timestamp{0});
    metadata.write(test_dir_);

    std::string content = read_file(test_dir_ / "metadata.json");
    EXPECT_TRUE(content.find("\"agents\": []") != std::string::npos);
}

TEST_F(PersistenceTest, DataCollectorFinalizeWritesMetadata) {
    {
        DataCollector collector(test_dir_);
        collector.metadata().set_simulation_config(Timestamp{10});
        collector.metadata().add_instrument(InstrumentID{1});
        collector.finalize(Timestamp{50000});
    }

    std::string content = read_file(test_dir_ / "metadata.json");
    EXPECT_TRUE(content.find("\"duration\": 50000") != std::string::npos);
}

// =============================================================================
// Move Semantics Tests (ensure no double-free issues)
// =============================================================================

TEST_F(PersistenceTest, CSVWriterMoveConstructor) {
    CSVWriter writer1(test_dir_);
    writer1.write_delta(OrderDelta{.timestamp = Timestamp{100},
                                   .sequence_num = EventSequenceNumber{1},
                                   .type = DeltaType::ADD,
                                   .order_id = OrderID{1},
                                   .client_id = ClientID{1},
                                   .instrument_id = InstrumentID{1},
                                   .side = OrderSide::BUY,
                                   .price = Price{1000},
                                   .quantity = Quantity{50},
                                   .remaining_qty = Quantity{50}});

    CSVWriter writer2(std::move(writer1));
    writer2.write_delta(OrderDelta{.timestamp = Timestamp{200},
                                   .sequence_num = EventSequenceNumber{2},
                                   .type = DeltaType::ADD,
                                   .order_id = OrderID{2},
                                   .client_id = ClientID{1},
                                   .instrument_id = InstrumentID{1},
                                   .side = OrderSide::BUY,
                                   .price = Price{1000},
                                   .quantity = Quantity{50},
                                   .remaining_qty = Quantity{50}});

    writer2.flush();

    // Both records should be in the file
    EXPECT_EQ(count_data_lines(test_dir_ / "deltas.csv"), 2);
}
