#include <gtest/gtest.h>

#include "agents/informed_trader.hpp"
#include "simulation/fair_price.hpp"
#include "simulation/simulation_engine.hpp"

// Test access class to inspect InformedTrader internals
class InformedTraderTestAccess {
public:
    static const std::vector<TrackedOrder>& active_orders(const InformedTrader& trader) {
        return trader.active_orders_;
    }

    static bool is_order_stale(const InformedTrader& trader, const TrackedOrder& order,
                               Price fair) {
        return trader.is_order_stale(order, fair);
    }
};

class MockContextWithBook : public AgentContext {
public:
    explicit MockContextWithBook(Price fair = Price{100}) : fair_price_(fair) {}

    void submit_order(InstrumentID, Quantity, Price p, OrderSide s, OrderType) override {
        submit_count++;
        last_submit_price = p;
        last_submit_side = s;
    }
    void cancel_order(OrderID id) override { cancelled_orders.push_back(id); }
    void modify_order(OrderID, Quantity, Price) override {}
    void schedule_wakeup(Timestamp t) override { next_wakeup = t; }
    const OrderBook& get_order_book(InstrumentID) const override { return book; }
    Timestamp now() const override { return current_time; }
    Price fair_price() const override { return fair_price_; }

    void set_fair_price(Price p) { fair_price_ = p; }
    void set_time(Timestamp t) { current_time = t; }

    void add_bid(Price price, Quantity qty) {
        Order order{.order_id = OrderID{next_order_id_++},
                    .client_id = ClientID{99}, // Some other trader
                    .quantity = qty,
                    .price = price,
                    .timestamp = Timestamp{0},
                    .instrument_id = InstrumentID{1},
                    .side = OrderSide::BUY,
                    .type = OrderType::LIMIT,
                    .status = OrderStatus::NEW};
        book.bids[price].push_back(order);
    }

    void add_ask(Price price, Quantity qty) {
        Order order{.order_id = OrderID{next_order_id_++},
                    .client_id = ClientID{99},
                    .quantity = qty,
                    .price = price,
                    .timestamp = Timestamp{0},
                    .instrument_id = InstrumentID{1},
                    .side = OrderSide::SELL,
                    .type = OrderType::LIMIT,
                    .status = OrderStatus::NEW};
        book.asks[price].push_back(order);
    }

    int submit_count = 0;
    Price last_submit_price{0};
    OrderSide last_submit_side = OrderSide::BUY;
    std::vector<OrderID> cancelled_orders;
    Timestamp next_wakeup{0};
    OrderBook book;

private:
    Price fair_price_;
    Timestamp current_time{0};
    std::uint64_t next_order_id_ = 100;
};

class InformedTraderTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = InformedTraderConfig{
            .instrument = InstrumentID{1},
            .min_quantity = Quantity{1},
            .max_quantity = Quantity{10},
            .min_interval = Timestamp{100},
            .max_interval = Timestamp{100},
            .min_edge = Price{5},
            .observation_noise = 0.0, // No noise for deterministic tests
            .stale_order_threshold = Price{20}};
    }

    InformedTraderConfig config;
};

// =============================================================================
// Basic Functionality Tests
// =============================================================================

TEST_F(InformedTraderTest, NoTradeWhenNoEdge) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{100});

    // Set up book where fair price is at midpoint (no edge)
    ctx.add_bid(Price{98}, Quantity{100});
    ctx.add_ask(Price{102}, Quantity{100});

    // Fair price is 100, best ask is 102
    // observed (100) > best_ask (102) + min_edge (5)? 100 > 107? No
    // observed (100) + min_edge (5) < best_bid (98)? 105 < 98? No
    trader.on_wakeup(ctx);

    EXPECT_EQ(ctx.submit_count, 0);
}

TEST_F(InformedTraderTest, BuysWhenFairPriceAboveBestAskPlusEdge) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{120}); // Fair price is 120

    ctx.add_bid(Price{98}, Quantity{100});
    ctx.add_ask(Price{102}, Quantity{100}); // Best ask is 102

    // observed (120) > best_ask (102) + min_edge (5)? 120 > 107? Yes!
    trader.on_wakeup(ctx);

    EXPECT_EQ(ctx.submit_count, 1);
    EXPECT_EQ(ctx.last_submit_side, OrderSide::BUY);
    EXPECT_EQ(ctx.last_submit_price, Price{102}); // Buys at best ask
}

TEST_F(InformedTraderTest, SellsWhenFairPriceBelowBestBidMinusEdge) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{80}); // Fair price is 80

    ctx.add_bid(Price{98}, Quantity{100}); // Best bid is 98
    ctx.add_ask(Price{102}, Quantity{100});

    // observed (80) + min_edge (5) < best_bid (98)? 85 < 98? Yes!
    trader.on_wakeup(ctx);

    EXPECT_EQ(ctx.submit_count, 1);
    EXPECT_EQ(ctx.last_submit_side, OrderSide::SELL);
    EXPECT_EQ(ctx.last_submit_price, Price{98}); // Sells at best bid
}

TEST_F(InformedTraderTest, SchedulesNextWakeup) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{100});
    ctx.set_time(Timestamp{50});

    trader.on_wakeup(ctx);

    // With min_interval = max_interval = 100, next wakeup should be at 150
    EXPECT_EQ(ctx.next_wakeup, Timestamp{150});
}

TEST_F(InformedTraderTest, OrderAcceptedTracksOrder) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{120});
    ctx.add_ask(Price{102}, Quantity{100});

    // Submit a buy order
    trader.on_wakeup(ctx);
    ASSERT_EQ(ctx.submit_count, 1);

    // Simulate order acceptance
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    const auto& active = InformedTraderTestAccess::active_orders(trader);
    EXPECT_EQ(active.size(), 1);
    EXPECT_EQ(active[0].order_id, OrderID{1});
    EXPECT_EQ(active[0].price, Price{102});
    EXPECT_EQ(active[0].side, OrderSide::BUY);
}

TEST_F(InformedTraderTest, OrderCancelledRemovesFromTracking) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{120});
    ctx.add_ask(Price{102}, Quantity{100});

    // Submit and accept
    trader.on_wakeup(ctx);
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    // Cancel
    OrderCancelled cancelled{.timestamp = Timestamp{0},
                             .order_id = OrderID{1},
                             .agent_id = ClientID{1},
                             .remaining_quantity = Quantity{5}};
    trader.on_order_cancelled(ctx, cancelled);

    const auto& active = InformedTraderTestAccess::active_orders(trader);
    EXPECT_TRUE(active.empty());
}

TEST_F(InformedTraderTest, TradeRemovesFilledOrder) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{120});
    ctx.add_ask(Price{102}, Quantity{100});

    // Submit and accept
    trader.on_wakeup(ctx);
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    // Trade fills order
    Trade trade{.timestamp = Timestamp{0},
                .trade_id = TradeID{1},
                .instrument_id = InstrumentID{1},
                .buyer_order_id = OrderID{1},
                .seller_order_id = OrderID{2},
                .buyer_id = ClientID{1},
                .seller_id = ClientID{99},
                .quantity = Quantity{5},
                .price = Price{102}};
    trader.on_trade(ctx, trade);

    const auto& active = InformedTraderTestAccess::active_orders(trader);
    EXPECT_TRUE(active.empty());
}

// =============================================================================
// Stale Order Cancellation Tests
// =============================================================================

TEST_F(InformedTraderTest, IsOrderStaleReturnsFalseWhenThresholdIsZero) {
    config.stale_order_threshold = Price{0};
    InformedTrader trader(ClientID{1}, config, 42);

    TrackedOrder order{.order_id = OrderID{1}, .price = Price{100}, .side = OrderSide::BUY};

    EXPECT_FALSE(InformedTraderTestAccess::is_order_stale(trader, order, Price{50}));
    EXPECT_FALSE(InformedTraderTestAccess::is_order_stale(trader, order, Price{200}));
}

TEST_F(InformedTraderTest, BuyOrderStaleWhenPriceTooFarAboveFair) {
    InformedTrader trader(ClientID{1}, config, 42);

    // BUY order at 100, threshold is 20
    TrackedOrder order{.order_id = OrderID{1}, .price = Price{100}, .side = OrderSide::BUY};

    // Fair price at 79: 100 > 79 + 20 = 99? Yes, stale
    EXPECT_TRUE(InformedTraderTestAccess::is_order_stale(trader, order, Price{79}));

    // Fair price at 81: 100 > 81 + 20 = 101? No
    EXPECT_FALSE(InformedTraderTestAccess::is_order_stale(trader, order, Price{81}));

    // Fair price at 100: 100 > 100 + 20? No
    EXPECT_FALSE(InformedTraderTestAccess::is_order_stale(trader, order, Price{100}));
}

TEST_F(InformedTraderTest, SellOrderStaleWhenPriceTooFarBelowFair) {
    InformedTrader trader(ClientID{1}, config, 42);

    // SELL order at 100, threshold is 20
    TrackedOrder order{.order_id = OrderID{1}, .price = Price{100}, .side = OrderSide::SELL};

    // Fair price at 121: 100 + 20 < 121? 120 < 121? Yes, stale
    EXPECT_TRUE(InformedTraderTestAccess::is_order_stale(trader, order, Price{121}));

    // Fair price at 119: 100 + 20 < 119? 120 < 119? No
    EXPECT_FALSE(InformedTraderTestAccess::is_order_stale(trader, order, Price{119}));

    // Fair price at 100: not stale
    EXPECT_FALSE(InformedTraderTestAccess::is_order_stale(trader, order, Price{100}));
}

TEST_F(InformedTraderTest, OnWakeupCancelsStaleOrders) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{120});
    ctx.add_ask(Price{102}, Quantity{100});

    // Submit and accept a BUY order at price 102
    trader.on_wakeup(ctx);
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    const auto& active = InformedTraderTestAccess::active_orders(trader);
    ASSERT_EQ(active.size(), 1);
    EXPECT_EQ(active[0].price, Price{102});
    EXPECT_EQ(active[0].side, OrderSide::BUY);

    // Move fair price down to make the BUY order stale
    // BUY at 102 is stale when fair < 102 - 20 = 82
    ctx.set_fair_price(Price{80});
    ctx.cancelled_orders.clear();

    // Trigger wakeup - should cancel stale order
    trader.on_wakeup(ctx);

    EXPECT_EQ(ctx.cancelled_orders.size(), 1);
    EXPECT_EQ(ctx.cancelled_orders[0], OrderID{1});
}

// =============================================================================
// Integration Test with SimulationEngine and DummyFairPriceSource
// =============================================================================

TEST_F(InformedTraderTest, TradesWhenEdgeExistsWithDummyFairPrice) {
    SimulationEngine engine;
    engine.add_instrument(InstrumentID{1});

    // Use DummyFairPriceSource to control the fair price
    auto dummy_source = std::make_unique<DummyFairPriceSource>(Price{120});
    engine.set_fair_price_source(std::move(dummy_source));

    // Add informed trader with deterministic quantity
    config.min_edge = Price{5};
    config.min_quantity = Quantity{5};
    config.max_quantity = Quantity{5}; // Deterministic quantity
    config.min_interval = Timestamp{1000};
    config.max_interval = Timestamp{1000};
    engine.add_agent<InformedTrader>(ClientID{1}, config, 42);

    // Add liquidity: sell order at 100
    engine.scheduler().schedule(
        OrderSubmitted{.timestamp = Timestamp{0},
                       .agent_id = ClientID{99},
                       .instrument_id = InstrumentID{1},
                       .quantity = Quantity{100},
                       .price = Price{100},
                       .side = OrderSide::SELL,
                       .type = OrderType::LIMIT});

    engine.step();

    // Schedule informed trader wakeup
    // Fair price is 120, best ask is 100, edge needed is 5
    // 120 > 100 + 5? Yes, informed trader should buy 5 units
    engine.scheduler().schedule(AgentWakeup{.timestamp = Timestamp{1}, .agent_id = ClientID{1}});

    // Run until trade is processed
    engine.run_until(Timestamp{10});

    // Verify trade occurred: informed trader bought 5 units
    const auto& pnl = engine.get_pnl(ClientID{1});
    EXPECT_EQ(pnl.long_position, Quantity{5});
    EXPECT_EQ(pnl.short_position, Quantity{0});
}

TEST_F(InformedTraderTest, NoTradeWhenNoEdgeWithDummyFairPrice) {
    SimulationEngine engine;
    engine.add_instrument(InstrumentID{1});

    // Use DummyFairPriceSource - fair price equals best ask, no edge
    auto dummy_source = std::make_unique<DummyFairPriceSource>(Price{100});
    engine.set_fair_price_source(std::move(dummy_source));

    // Add informed trader
    config.min_edge = Price{5};
    config.min_interval = Timestamp{1000};
    config.max_interval = Timestamp{1000};
    engine.add_agent<InformedTrader>(ClientID{1}, config, 42);

    // Add liquidity: sell order at 100
    engine.scheduler().schedule(
        OrderSubmitted{.timestamp = Timestamp{0},
                       .agent_id = ClientID{99},
                       .instrument_id = InstrumentID{1},
                       .quantity = Quantity{100},
                       .price = Price{100},
                       .side = OrderSide::SELL,
                       .type = OrderType::LIMIT});

    engine.step();

    // Schedule informed trader wakeup
    // Fair price is 100, best ask is 100, edge needed is 5
    // 100 > 100 + 5? No, informed trader should NOT buy
    engine.scheduler().schedule(AgentWakeup{.timestamp = Timestamp{1}, .agent_id = ClientID{1}});

    engine.run_until(Timestamp{10});

    // Verify no trade occurred
    const auto& pnl = engine.get_pnl(ClientID{1});
    EXPECT_EQ(pnl.long_position, Quantity{0});
    EXPECT_EQ(pnl.short_position, Quantity{0});
}

TEST_F(InformedTraderTest, DoesNotTradeOnEmptyBook) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{120});

    // Empty book - no bids or asks
    trader.on_wakeup(ctx);

    EXPECT_EQ(ctx.submit_count, 0);
}

TEST_F(InformedTraderTest, OnlyBuysWhenAsksAvailable) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{120}); // High fair price

    // Only bids, no asks
    ctx.add_bid(Price{98}, Quantity{100});

    trader.on_wakeup(ctx);

    // Can't buy without asks
    EXPECT_EQ(ctx.submit_count, 0);
}

TEST_F(InformedTraderTest, OnlySellsWhenBidsAvailable) {
    InformedTrader trader(ClientID{1}, config, 42);
    MockContextWithBook ctx(Price{80}); // Low fair price

    // Only asks, no bids
    ctx.add_ask(Price{102}, Quantity{100});

    trader.on_wakeup(ctx);

    // Can't sell without bids
    EXPECT_EQ(ctx.submit_count, 0);
}
