#include <gtest/gtest.h>

#include "simulation/simulation_engine.hpp"

class SimulationEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<SimulationEngine>(Timestamp{0});
        engine->add_instrument(InstrumentID{1});
    }

    void TearDown() override { engine.reset(); }

    std::unique_ptr<SimulationEngine> engine;

    void schedule_order(Timestamp ts, ClientID client, Quantity qty, Price price,
                        OrderSide side, OrderType type = OrderType::LIMIT) {
        engine->scheduler().schedule(
            OrderSubmitted{.timestamp = ts,
                           .agent_id = client,
                           .instrument_id = InstrumentID{1},
                           .quantity = qty,
                           .price = price,
                           .side = side,
                           .type = type});
    }
};

// =============================================================================
// Basic Order Processing
// =============================================================================

TEST_F(SimulationEngineTest, SingleOrderAddsToBook) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000}, OrderSide::BUY);

    engine->run_until(Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1);
    EXPECT_EQ(book.asks.size(), 0);
}

TEST_F(SimulationEngineTest, BuyAndSellOrdersAddToBook) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{990}, OrderSide::BUY);
    schedule_order(Timestamp{100}, ClientID{2}, Quantity{50}, Price{1010}, OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1);
    EXPECT_EQ(book.asks.size(), 1);
}

TEST_F(SimulationEngineTest, MatchingOrdersCrossSpread) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000}, OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{50}, Price{1000}, OrderSide::SELL);

    engine->run_until(Timestamp{300});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 0);
    EXPECT_EQ(book.asks.size(), 0);
}

TEST_F(SimulationEngineTest, PartialFillLeavesRemainder) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000}, OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{30}, Price{1000}, OrderSide::SELL);

    engine->run_until(Timestamp{300});

    const auto& book = engine->get_order_book(InstrumentID{1});
    ASSERT_EQ(book.bids.size(), 1);
    auto it = book.bids.begin();
    EXPECT_EQ(it->second.front().quantity, Quantity{70});
}

// =============================================================================
// Timestamp Ordering
// =============================================================================

TEST_F(SimulationEngineTest, OrdersProcessedInTimestampOrder) {
    // Later timestamp scheduled first
    schedule_order(Timestamp{300}, ClientID{2}, Quantity{50}, Price{1010}, OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{990}, OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{3}, Quantity{50}, Price{1005}, OrderSide::BUY);

    engine->run_until(Timestamp{150});

    // Only first order should be processed
    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1);
    EXPECT_EQ(book.asks.size(), 0);

    engine->run_until(Timestamp{250});

    // Now second order should be processed
    EXPECT_EQ(book.bids.size(), 2);
    EXPECT_EQ(book.asks.size(), 0);
}

TEST_F(SimulationEngineTest, TimeAdvancesWithEvents) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000}, OrderSide::BUY);
    schedule_order(Timestamp{500}, ClientID{2}, Quantity{50}, Price{1000}, OrderSide::SELL);

    EXPECT_EQ(engine->now(), Timestamp{0});

    engine->step();
    EXPECT_EQ(engine->now(), Timestamp{100});

    engine->step();
    EXPECT_EQ(engine->now(), Timestamp{500});
}

// =============================================================================
// Multiple Price Levels
// =============================================================================

TEST_F(SimulationEngineTest, MultipleBidLevels) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{10}, Price{990}, OrderSide::BUY);
    schedule_order(Timestamp{100}, ClientID{2}, Quantity{20}, Price{980}, OrderSide::BUY);
    schedule_order(Timestamp{100}, ClientID{3}, Quantity{30}, Price{970}, OrderSide::BUY);

    engine->run_until(Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 3);
}

TEST_F(SimulationEngineTest, MultipleAskLevels) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{10}, Price{1010}, OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{2}, Quantity{20}, Price{1020}, OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{3}, Quantity{30}, Price{1030}, OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.asks.size(), 3);
}

TEST_F(SimulationEngineTest, AggressiveOrderSweepsMultipleLevels) {
    // Setup asks at different levels
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{10}, Price{1000}, OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{2}, Quantity{10}, Price{1001}, OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{3}, Quantity{10}, Price{1002}, OrderSide::SELL);

    // Large buy sweeps all levels
    schedule_order(Timestamp{200}, ClientID{4}, Quantity{30}, Price{1005}, OrderSide::BUY);

    engine->run_until(Timestamp{300});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.asks.size(), 0);
    EXPECT_EQ(book.bids.size(), 0);
}

// =============================================================================
// Self-Trade Prevention
// =============================================================================

TEST_F(SimulationEngineTest, SelfTradePreventedSameClient) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000}, OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{1}, Quantity{50}, Price{1000}, OrderSide::SELL);

    engine->run_until(Timestamp{300});

    // Both orders should remain since self-trade is prevented
    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1);
    EXPECT_EQ(book.asks.size(), 1);
}

// =============================================================================
// Market Orders
// =============================================================================

TEST_F(SimulationEngineTest, MarketOrderMatchesImmediately) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000}, OrderSide::SELL);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{50}, Price{0}, OrderSide::BUY,
                   OrderType::MARKET);

    engine->run_until(Timestamp{300});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 0);
    EXPECT_EQ(book.asks.size(), 0);
}

TEST_F(SimulationEngineTest, MarketOrderNotAddedToBook) {
    // Market order with no liquidity
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{0}, OrderSide::BUY,
                   OrderType::MARKET);

    engine->run_until(Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 0);
}

// =============================================================================
// Latency
// =============================================================================

class SimulationEngineLatencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<SimulationEngine>(Timestamp{50}); // 50 unit latency
        engine->add_instrument(InstrumentID{1});
    }

    std::unique_ptr<SimulationEngine> engine;
};

TEST_F(SimulationEngineLatencyTest, LatencyDelaysOrderProcessing) {
    // Manually schedule an order at t=0
    engine->scheduler().schedule(
        OrderSubmitted{.timestamp = Timestamp{0},
                       .agent_id = ClientID{1},
                       .instrument_id = InstrumentID{1},
                       .quantity = Quantity{50},
                       .price = Price{1000},
                       .side = OrderSide::BUY,
                       .type = OrderType::LIMIT});

    engine->run_until(Timestamp{100});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1);
}

// =============================================================================
// Run Until Behavior
// =============================================================================

TEST_F(SimulationEngineTest, RunUntilStopsAtCorrectTime) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000}, OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{50}, Price{1001}, OrderSide::BUY);
    schedule_order(Timestamp{300}, ClientID{3}, Quantity{50}, Price{1002}, OrderSide::BUY);

    engine->run_until(Timestamp{250});

    EXPECT_EQ(engine->now(), Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 2);
}

TEST_F(SimulationEngineTest, StepProcessesSingleEvent) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000}, OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{50}, Price{1001}, OrderSide::BUY);

    engine->step();

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1);
    EXPECT_EQ(engine->now(), Timestamp{100});
}

TEST_F(SimulationEngineTest, EmptySchedulerDoesNothing) {
    engine->step();
    EXPECT_EQ(engine->now(), Timestamp{0});

    engine->run_until(Timestamp{1000});
    EXPECT_EQ(engine->now(), Timestamp{0});
}

// =============================================================================
// Unknown Instrument
// =============================================================================

TEST_F(SimulationEngineTest, UnknownInstrumentReturnsEmptyBook) {
    const auto& book = engine->get_order_book(InstrumentID{999});
    EXPECT_EQ(book.bids.size(), 0);
    EXPECT_EQ(book.asks.size(), 0);
}
