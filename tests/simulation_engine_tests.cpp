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
        engine->scheduler().schedule(OrderSubmitted{.timestamp = ts,
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
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);

    engine->run_until(Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1);
    EXPECT_EQ(book.asks.size(), 0);
}

TEST_F(SimulationEngineTest, BuyAndSellOrdersAddToBook) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{990}, OrderSide::BUY);
    schedule_order(Timestamp{100}, ClientID{2}, Quantity{50}, Price{1010},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 1);
    EXPECT_EQ(book.asks.size(), 1);
}

TEST_F(SimulationEngineTest, MatchingOrdersCrossSpread) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{50}, Price{1000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{300});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 0);
    EXPECT_EQ(book.asks.size(), 0);
}

TEST_F(SimulationEngineTest, PartialFillLeavesRemainder) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{30}, Price{1000},
                   OrderSide::SELL);

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
    schedule_order(Timestamp{300}, ClientID{2}, Quantity{50}, Price{1010},
                   OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{990}, OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{3}, Quantity{50}, Price{1005},
                   OrderSide::BUY);

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
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{500}, ClientID{2}, Quantity{50}, Price{1000},
                   OrderSide::SELL);

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
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{10}, Price{1010},
                   OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{2}, Quantity{20}, Price{1020},
                   OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{3}, Quantity{30}, Price{1030},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.asks.size(), 3);
}

TEST_F(SimulationEngineTest, AggressiveOrderSweepsMultipleLevels) {
    // Setup asks at different levels
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{10}, Price{1000},
                   OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{2}, Quantity{10}, Price{1001},
                   OrderSide::SELL);
    schedule_order(Timestamp{100}, ClientID{3}, Quantity{10}, Price{1002},
                   OrderSide::SELL);

    // Large buy sweeps all levels
    schedule_order(Timestamp{200}, ClientID{4}, Quantity{30}, Price{1005},
                   OrderSide::BUY);

    engine->run_until(Timestamp{300});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.asks.size(), 0);
    EXPECT_EQ(book.bids.size(), 0);
}

// =============================================================================
// Self-Trade Prevention
// =============================================================================

TEST_F(SimulationEngineTest, SelfTradePreventedSameClient) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::SELL);

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
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::SELL);
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
    engine->scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
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
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{50}, Price{1001},
                   OrderSide::BUY);
    schedule_order(Timestamp{300}, ClientID{3}, Quantity{50}, Price{1002},
                   OrderSide::BUY);

    engine->run_until(Timestamp{250});

    EXPECT_EQ(engine->now(), Timestamp{200});

    const auto& book = engine->get_order_book(InstrumentID{1});
    EXPECT_EQ(book.bids.size(), 2);
}

TEST_F(SimulationEngineTest, StepProcessesSingleEvent) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{200}, ClientID{2}, Quantity{50}, Price{1001},
                   OrderSide::BUY);

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

// =============================================================================
// P&L Tracking
// =============================================================================

class PnLTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<SimulationEngine>(Timestamp{0});
        engine->add_instrument(InstrumentID{1});
    }

    std::unique_ptr<SimulationEngine> engine;

    void schedule_order(Timestamp ts, ClientID client, Quantity qty, Price price,
                        OrderSide side, OrderType type = OrderType::LIMIT) {
        engine->scheduler().schedule(OrderSubmitted{.timestamp = ts,
                                                    .agent_id = client,
                                                    .instrument_id = InstrumentID{1},
                                                    .quantity = qty,
                                                    .price = price,
                                                    .side = side,
                                                    .type = type});
    }
};

TEST_F(PnLTest, NoPnLBeforeAnyTrades) {
    const auto& pnl = engine->get_pnl(ClientID{1});
    EXPECT_EQ(pnl.long_position.value(), 0);
    EXPECT_EQ(pnl.short_position.value(), 0);
    EXPECT_EQ(pnl.cash, 0);
    EXPECT_EQ(pnl.net_position(), 0);
}

TEST_F(PnLTest, BuyerGetsLongPositionAndNegativeCash) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{50}, Price{1000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& buyer_pnl = engine->get_pnl(ClientID{1});
    EXPECT_EQ(buyer_pnl.long_position.value(), 50);
    EXPECT_EQ(buyer_pnl.short_position.value(), 0);
    EXPECT_EQ(buyer_pnl.cash, -50000); // -50 * 1000
    EXPECT_EQ(buyer_pnl.net_position(), 50);
}

TEST_F(PnLTest, SellerGetsShortPositionAndPositiveCash) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{50}, Price{1000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& seller_pnl = engine->get_pnl(ClientID{2});
    EXPECT_EQ(seller_pnl.long_position.value(), 0);
    EXPECT_EQ(seller_pnl.short_position.value(), 50);
    EXPECT_EQ(seller_pnl.cash, 50000); // +50 * 1000
    EXPECT_EQ(seller_pnl.net_position(), -50);
}

TEST_F(PnLTest, UnrealizedPnLCalculation) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{100}, Price{1000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& buyer_pnl = engine->get_pnl(ClientID{1});

    // Mark price higher than entry -> profit for long
    EXPECT_EQ(buyer_pnl.unrealized_pnl(Price{1100}), 100 * 1100); // 110000

    // Mark price lower than entry -> loss for long
    EXPECT_EQ(buyer_pnl.unrealized_pnl(Price{900}), 100 * 900); // 90000
}

TEST_F(PnLTest, TotalPnLCombinesCashAndUnrealized) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{100}, Price{1000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& buyer_pnl = engine->get_pnl(ClientID{1});
    // Cash: -100000, Position: 100 long

    // Mark at entry price -> breakeven
    EXPECT_EQ(buyer_pnl.total_pnl(Price{1000}), 0); // -100000 + 100*1000 = 0

    // Mark higher -> profit
    EXPECT_EQ(buyer_pnl.total_pnl(Price{1100}), 10000); // -100000 + 100*1100 = 10000

    // Mark lower -> loss
    EXPECT_EQ(buyer_pnl.total_pnl(Price{900}), -10000); // -100000 + 100*900 = -10000
}

TEST_F(PnLTest, MultipleFillsAccumulate) {
    // Client 1 buys twice
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{50}, Price{1000},
                   OrderSide::SELL);
    schedule_order(Timestamp{200}, ClientID{1}, Quantity{30}, Price{1010},
                   OrderSide::BUY);
    schedule_order(Timestamp{201}, ClientID{3}, Quantity{30}, Price{1010},
                   OrderSide::SELL);

    engine->run_until(Timestamp{300});

    const auto& buyer_pnl = engine->get_pnl(ClientID{1});
    EXPECT_EQ(buyer_pnl.long_position.value(), 80);      // 50 + 30
    EXPECT_EQ(buyer_pnl.cash, -(50 * 1000 + 30 * 1010)); // -80300
}

TEST_F(PnLTest, BuyAndSellByShameClientNetOut) {
    // Client 1 buys, then sells
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{100}, Price{1000},
                   OrderSide::SELL);
    schedule_order(Timestamp{200}, ClientID{1}, Quantity{100}, Price{1010},
                   OrderSide::SELL);
    schedule_order(Timestamp{201}, ClientID{3}, Quantity{100}, Price{1010},
                   OrderSide::BUY);

    engine->run_until(Timestamp{300});

    const auto& client1_pnl = engine->get_pnl(ClientID{1});
    EXPECT_EQ(client1_pnl.long_position.value(), 100);
    EXPECT_EQ(client1_pnl.short_position.value(), 100);
    EXPECT_EQ(client1_pnl.net_position(), 0);            // Flat
    EXPECT_EQ(client1_pnl.cash, -100000 + 101000);       // +1000 profit
    EXPECT_EQ(client1_pnl.total_pnl(Price{1000}), 1000); // Profit regardless of mark
}

TEST_F(PnLTest, PartialFillUpdatesParticipantPnL) {
    // Large buy order, small sell -> partial fill
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{30}, Price{1000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& buyer_pnl = engine->get_pnl(ClientID{1});
    EXPECT_EQ(buyer_pnl.long_position.value(), 30); // Only filled 30
    EXPECT_EQ(buyer_pnl.cash, -30000);

    const auto& seller_pnl = engine->get_pnl(ClientID{2});
    EXPECT_EQ(seller_pnl.short_position.value(), 30);
    EXPECT_EQ(seller_pnl.cash, 30000);
}

TEST_F(PnLTest, ZeroMarkPriceEdgeCase) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{100}, Price{1000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& buyer_pnl = engine->get_pnl(ClientID{1});
    // Mark at 0 -> total loss of cash spent
    EXPECT_EQ(buyer_pnl.unrealized_pnl(Price{0}), 0);
    EXPECT_EQ(buyer_pnl.total_pnl(Price{0}), -100000);

    const auto& seller_pnl = engine->get_pnl(ClientID{2});
    // Short at 0 -> keep all cash, no liability
    EXPECT_EQ(seller_pnl.unrealized_pnl(Price{0}), 0);
    EXPECT_EQ(seller_pnl.total_pnl(Price{0}), 100000);
}

TEST_F(PnLTest, LargePositionValues) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{1000000}, Price{50000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{1000000}, Price{50000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    const auto& buyer_pnl = engine->get_pnl(ClientID{1});
    EXPECT_EQ(buyer_pnl.long_position.value(), 1000000);
    EXPECT_EQ(buyer_pnl.cash, -50000000000LL); // -50 billion
    EXPECT_EQ(buyer_pnl.total_pnl(Price{50000}), 0);
}

TEST_F(PnLTest, AllPnLReturnsAllParticipants) {
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{50}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{50}, Price{1000},
                   OrderSide::SELL);
    schedule_order(Timestamp{200}, ClientID{3}, Quantity{30}, Price{1010},
                   OrderSide::BUY);
    schedule_order(Timestamp{201}, ClientID{4}, Quantity{30}, Price{1010},
                   OrderSide::SELL);

    engine->run_until(Timestamp{300});

    const auto& all = engine->all_pnl();
    EXPECT_EQ(all.size(), 4);
    EXPECT_TRUE(all.contains(ClientID{1}));
    EXPECT_TRUE(all.contains(ClientID{2}));
    EXPECT_TRUE(all.contains(ClientID{3}));
    EXPECT_TRUE(all.contains(ClientID{4}));
}

TEST_F(PnLTest, UnknownClientReturnsEmptyPnL) {
    const auto& pnl = engine->get_pnl(ClientID{999});
    EXPECT_EQ(pnl.long_position.value(), 0);
    EXPECT_EQ(pnl.short_position.value(), 0);
    EXPECT_EQ(pnl.cash, 0);
}

TEST_F(PnLTest, CashSumsToZeroAcrossAllParticipants) {
    // In a closed system, total cash flow should be zero
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{60}, Price{1000},
                   OrderSide::SELL);
    schedule_order(Timestamp{102}, ClientID{3}, Quantity{40}, Price{1000},
                   OrderSide::SELL);

    engine->run_until(Timestamp{200});

    std::int64_t total_cash = 0;
    for (const auto& [client_id, pnl] : engine->all_pnl()) {
        total_cash += pnl.cash;
    }
    EXPECT_EQ(total_cash, 0);
}

TEST_F(PnLTest, NetPositionSumsToZeroAcrossAllParticipants) {
    // In a closed system, net positions should sum to zero
    schedule_order(Timestamp{100}, ClientID{1}, Quantity{100}, Price{1000},
                   OrderSide::BUY);
    schedule_order(Timestamp{101}, ClientID{2}, Quantity{100}, Price{1000},
                   OrderSide::SELL);
    schedule_order(Timestamp{200}, ClientID{3}, Quantity{50}, Price{1010},
                   OrderSide::BUY);
    schedule_order(Timestamp{201}, ClientID{4}, Quantity{50}, Price{1010},
                   OrderSide::SELL);

    engine->run_until(Timestamp{300});

    std::int64_t total_position = 0;
    for (const auto& [client_id, pnl] : engine->all_pnl()) {
        total_position += pnl.net_position();
    }
    EXPECT_EQ(total_position, 0);
}
