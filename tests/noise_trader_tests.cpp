#include <gtest/gtest.h>

#include "agents/noise_trader.hpp"
#include "simulation/fair_price.hpp"
#include "simulation/simulation_engine.hpp"

// Test access class to inspect NoiseTrader internals
class NoiseTraderTestAccess {
public:
    static const std::vector<TrackedOrder>& active_orders(const NoiseTrader& trader) {
        return trader.active_orders_;
    }

    static bool is_order_stale(const NoiseTrader& trader, const TrackedOrder& order,
                               Price fair) {
        return trader.is_order_stale(order, fair);
    }
};

class MockContext : public AgentContext {
public:
    explicit MockContext(Price fair = Price{100}) : fair_price_(fair) {}

    void submit_order(InstrumentID, Quantity, Price, OrderSide, OrderType) override {
        submit_count++;
    }
    void cancel_order(OrderID id) override { cancelled_orders.push_back(id); }
    void modify_order(OrderID, Quantity, Price) override {}
    void schedule_wakeup(Timestamp t) override { next_wakeup = t; }
    const OrderBook& get_order_book(InstrumentID) const override {
        static OrderBook empty;
        return empty;
    }
    Timestamp now() const override { return current_time; }
    Price fair_price() const override { return fair_price_; }

    void set_fair_price(Price p) { fair_price_ = p; }
    void set_time(Timestamp t) { current_time = t; }

    int submit_count = 0;
    std::vector<OrderID> cancelled_orders;
    Timestamp next_wakeup{0};

private:
    Price fair_price_;
    Timestamp current_time{0};
};

class NoiseTraderTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = NoiseTraderConfig{
            .instrument = InstrumentID{1},
            .observation_noise = 0.0, // No noise for deterministic tests
            .spread = Price{10},
            .min_quantity = Quantity{1},
            .max_quantity = Quantity{10},
            .min_interval = Timestamp{100},
            .max_interval = Timestamp{100},
            .adverse_fill_threshold = Price{20},
            .stale_order_threshold = Price{200}};
    }

    NoiseTraderConfig config;
};

// =============================================================================
// Basic Functionality Tests
// =============================================================================

TEST_F(NoiseTraderTest, OnWakeupSubmitsOrder) {
    NoiseTrader trader(ClientID{1}, config, 42);
    MockContext ctx(Price{100});

    trader.on_wakeup(ctx);

    EXPECT_EQ(ctx.submit_count, 1);
}

TEST_F(NoiseTraderTest, OnWakeupSchedulesNextWakeup) {
    NoiseTrader trader(ClientID{1}, config, 42);
    MockContext ctx(Price{100});
    ctx.set_time(Timestamp{50});

    trader.on_wakeup(ctx);

    // With min_interval = max_interval = 100, next wakeup should be at 150
    EXPECT_EQ(ctx.next_wakeup, Timestamp{150});
}

TEST_F(NoiseTraderTest, OrderAcceptedTracksOrder) {
    NoiseTrader trader(ClientID{1}, config, 42);
    MockContext ctx(Price{100});

    // Submit an order first (this queues a pending submission)
    trader.on_wakeup(ctx);

    // Simulate order acceptance
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    const auto& active = NoiseTraderTestAccess::active_orders(trader);
    EXPECT_EQ(active.size(), 1);
    EXPECT_EQ(active[0].order_id, OrderID{1});
}

TEST_F(NoiseTraderTest, OrderCancelledRemovesFromTracking) {
    NoiseTrader trader(ClientID{1}, config, 42);
    MockContext ctx(Price{100});

    // Submit and accept an order
    trader.on_wakeup(ctx);
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    // Cancel the order
    OrderCancelled cancelled{.timestamp = Timestamp{0},
                             .order_id = OrderID{1},
                             .agent_id = ClientID{1},
                             .remaining_quantity = Quantity{5}};
    trader.on_order_cancelled(ctx, cancelled);

    const auto& active = NoiseTraderTestAccess::active_orders(trader);
    EXPECT_TRUE(active.empty());
}

TEST_F(NoiseTraderTest, TradeRemovesFilledOrder) {
    NoiseTrader trader(ClientID{1}, config, 42);
    MockContext ctx(Price{100});

    // Submit and accept an order
    trader.on_wakeup(ctx);
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    // Trade fully fills the order (quantity >= max_quantity from config)
    Trade trade{.timestamp = Timestamp{0},
                .trade_id = TradeID{1},
                .instrument_id = InstrumentID{1},
                .buyer_order_id = OrderID{1},
                .seller_order_id = OrderID{2},
                .buyer_id = ClientID{1},
                .seller_id = ClientID{2},
                .quantity = Quantity{10},
                .price = Price{100}};
    trader.on_trade(ctx, trade);

    const auto& active = NoiseTraderTestAccess::active_orders(trader);
    EXPECT_TRUE(active.empty());
}

TEST_F(NoiseTraderTest, PartialFillDecrementsRemainingQuantity) {
    config.min_quantity = Quantity{10};
    config.max_quantity = Quantity{10}; // Deterministic quantity
    NoiseTrader trader(ClientID{1}, config, 42);
    MockContext ctx(Price{100});

    // Submit and accept an order
    trader.on_wakeup(ctx);
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    // Partial fill (3 out of 10)
    Trade trade{.timestamp = Timestamp{0},
                .trade_id = TradeID{1},
                .instrument_id = InstrumentID{1},
                .buyer_order_id = OrderID{1},
                .seller_order_id = OrderID{2},
                .buyer_id = ClientID{1},
                .seller_id = ClientID{2},
                .quantity = Quantity{3},
                .price = Price{100}};
    trader.on_trade(ctx, trade);

    // Order should still be tracked with reduced quantity
    const auto& active = NoiseTraderTestAccess::active_orders(trader);
    ASSERT_EQ(active.size(), 1);
    EXPECT_EQ(active[0].remaining_quantity, Quantity{7});

    // Another partial fill (4 more, total 7 out of 10)
    Trade trade2{.timestamp = Timestamp{1},
                 .trade_id = TradeID{2},
                 .instrument_id = InstrumentID{1},
                 .buyer_order_id = OrderID{1},
                 .seller_order_id = OrderID{3},
                 .buyer_id = ClientID{1},
                 .seller_id = ClientID{3},
                 .quantity = Quantity{4},
                 .price = Price{100}};
    trader.on_trade(ctx, trade2);

    ASSERT_EQ(active.size(), 1);
    EXPECT_EQ(active[0].remaining_quantity, Quantity{3});

    // Final fill (3 remaining)
    Trade trade3{.timestamp = Timestamp{2},
                 .trade_id = TradeID{3},
                 .instrument_id = InstrumentID{1},
                 .buyer_order_id = OrderID{1},
                 .seller_order_id = OrderID{4},
                 .buyer_id = ClientID{1},
                 .seller_id = ClientID{4},
                 .quantity = Quantity{3},
                 .price = Price{100}};
    trader.on_trade(ctx, trade3);

    // Now the order should be removed
    EXPECT_TRUE(active.empty());
}

TEST_F(NoiseTraderTest, PartiallyFilledOrderStillCancelledWhenStale) {
    // This tests the bug where partially filled orders were removed from tracking
    // and never cancelled even when they became stale
    config.min_quantity = Quantity{10};
    config.max_quantity = Quantity{10};
    config.observation_noise = 0.0;
    config.spread = Price{5};
    config.adverse_fill_threshold = Price{20};
    NoiseTrader trader(ClientID{1}, config, 42);
    MockContext ctx(Price{100});

    // Submit and accept an order (will be around price 100 due to 0 noise, ±5 spread)
    trader.on_wakeup(ctx);
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    const auto& active = NoiseTraderTestAccess::active_orders(trader);
    ASSERT_EQ(active.size(), 1);
    Price order_price = active[0].price;
    OrderSide order_side = active[0].side;

    // Partial fill (only 3 of 10)
    Trade trade{
        .timestamp = Timestamp{0},
        .trade_id = TradeID{1},
        .instrument_id = InstrumentID{1},
        .buyer_order_id = order_side == OrderSide::BUY ? OrderID{1} : OrderID{2},
        .seller_order_id = order_side == OrderSide::SELL ? OrderID{1} : OrderID{2},
        .buyer_id = order_side == OrderSide::BUY ? ClientID{1} : ClientID{99},
        .seller_id = order_side == OrderSide::SELL ? ClientID{1} : ClientID{99},
        .quantity = Quantity{3},
        .price = order_price};
    trader.on_trade(ctx, trade);

    // Order should still be tracked with 7 remaining
    ASSERT_EQ(active.size(), 1);
    EXPECT_EQ(active[0].remaining_quantity, Quantity{7});

    // Move fair price to make order an adverse fill candidate
    // BUY is adverse if order_price > fair + threshold (bidding too high)
    // SELL is adverse if order_price + threshold < fair (asking too low)
    Price new_fair =
        order_side == OrderSide::BUY
            ? Price{order_price.value() - config.adverse_fill_threshold.value() - 10}
            : Price{order_price.value() + config.adverse_fill_threshold.value() + 10};
    ctx.set_fair_price(new_fair);

    // Wake up - should cancel the stale order
    ctx.cancelled_orders.clear();
    trader.on_wakeup(ctx);

    // Verify the partially filled order was still cancelled
    ASSERT_EQ(ctx.cancelled_orders.size(), 1);
    EXPECT_EQ(ctx.cancelled_orders[0], OrderID{1});
}

// =============================================================================
// Stale Order Cancellation Tests
// =============================================================================

TEST_F(NoiseTraderTest, IsOrderStaleReturnsFalseWhenThresholdsAreZero) {
    config.adverse_fill_threshold = Price{0};
    config.stale_order_threshold = Price{0};
    NoiseTrader trader(ClientID{1}, config, 42);

    TrackedOrder order{.order_id = OrderID{1},
                       .price = Price{100},
                       .side = OrderSide::BUY,
                       .remaining_quantity = Quantity{10}};

    // Even with large price deviation, should not be stale when threshold is 0
    EXPECT_FALSE(NoiseTraderTestAccess::is_order_stale(trader, order, Price{50}));
    EXPECT_FALSE(NoiseTraderTestAccess::is_order_stale(trader, order, Price{200}));
}

TEST_F(NoiseTraderTest, BuyOrderStaleWhenPriceTooFarAboveFair) {
    NoiseTrader trader(ClientID{1}, config, 42);

    // BUY order at 100, threshold is 20
    TrackedOrder order{.order_id = OrderID{1},
                       .price = Price{100},
                       .side = OrderSide::BUY,
                       .remaining_quantity = Quantity{10}};

    // Fair price at 80: 100 > 80 + 20, so stale
    EXPECT_TRUE(NoiseTraderTestAccess::is_order_stale(trader, order, Price{79}));

    // Fair price at 81: 100 > 81 + 20 = 101? No, so not stale
    EXPECT_FALSE(NoiseTraderTestAccess::is_order_stale(trader, order, Price{81}));

    // Fair price at 100: 100 > 100 + 20? No
    EXPECT_FALSE(NoiseTraderTestAccess::is_order_stale(trader, order, Price{100}));
}

TEST_F(NoiseTraderTest, SellOrderStaleWhenPriceTooFarBelowFair) {
    NoiseTrader trader(ClientID{1}, config, 42);

    // SELL order at 100, threshold is 20
    TrackedOrder order{.order_id = OrderID{1},
                       .price = Price{100},
                       .side = OrderSide::SELL,
                       .remaining_quantity = Quantity{10}};

    // Fair price at 121: 100 + 20 < 121, so stale (asking too low)
    EXPECT_TRUE(NoiseTraderTestAccess::is_order_stale(trader, order, Price{121}));

    // Fair price at 119: 100 + 20 < 119? 120 < 119? No
    EXPECT_FALSE(NoiseTraderTestAccess::is_order_stale(trader, order, Price{119}));

    // Fair price at 100: not stale
    EXPECT_FALSE(NoiseTraderTestAccess::is_order_stale(trader, order, Price{100}));
}

TEST_F(NoiseTraderTest, OnWakeupCancelsStaleOrders) {
    NoiseTrader trader(ClientID{1}, config, 42);
    MockContext ctx(Price{100});

    // Submit and accept a BUY order at price around 100
    trader.on_wakeup(ctx);
    OrderAccepted accepted{.timestamp = Timestamp{0},
                           .order_id = OrderID{1},
                           .agent_id = ClientID{1},
                           .instrument_id = InstrumentID{1}};
    trader.on_order_accepted(ctx, accepted);

    // Get the order's price
    const auto& active = NoiseTraderTestAccess::active_orders(trader);
    ASSERT_EQ(active.size(), 1);
    Price order_price = active[0].price;
    OrderSide order_side = active[0].side;

    // Move fair price to make the order an adverse fill candidate
    if (order_side == OrderSide::BUY) {
        // BUY order is adverse when price > fair + threshold
        // So set fair to order_price - threshold - 1
        ctx.set_fair_price(
            Price{order_price.value() - config.adverse_fill_threshold.value() - 1});
    } else {
        // SELL order is adverse when price + threshold < fair
        // So set fair to order_price + threshold + 1
        ctx.set_fair_price(
            Price{order_price.value() + config.adverse_fill_threshold.value() + 1});
    }

    // Trigger another wakeup which should cancel stale orders
    ctx.cancelled_orders.clear();
    trader.on_wakeup(ctx);

    EXPECT_EQ(ctx.cancelled_orders.size(), 1);
    EXPECT_EQ(ctx.cancelled_orders[0], OrderID{1});
}

// =============================================================================
// Integration Test with SimulationEngine and DummyFairPriceSource
// =============================================================================

TEST_F(NoiseTraderTest, StaleOrderCancellationWithSimulationEngine) {
    SimulationEngine engine;
    engine.add_instrument(InstrumentID{1});

    // Use DummyFairPriceSource so we can control the price
    auto dummy_source = std::make_unique<DummyFairPriceSource>(Price{100});
    DummyFairPriceSource* source_ptr = dummy_source.get();
    engine.set_fair_price_source(std::move(dummy_source));

    // Add noise trader with longer interval so we can control timing
    config.adverse_fill_threshold = Price{10};
    config.min_interval = Timestamp{1000};
    config.max_interval = Timestamp{1000};
    auto& trader = engine.add_agent<NoiseTrader>(ClientID{1}, config, 42);

    // Schedule initial wakeup
    engine.scheduler().schedule(
        AgentWakeup{.timestamp = Timestamp{1}, .agent_id = ClientID{1}});

    // Run until order is submitted and accepted
    engine.run_until(Timestamp{10});

    // Check that trader has tracked exactly one active order
    const auto& active = NoiseTraderTestAccess::active_orders(trader);
    ASSERT_EQ(active.size(), 1);

    Price order_price = active[0].price;
    OrderSide order_side = active[0].side;
    OrderID order_id = active[0].order_id;

    // Verify order is in the book
    const auto& book = engine.get_order_book(InstrumentID{1});
    size_t total_orders = 0;
    for (const auto& [_, orders] : book.bids) {
        total_orders += orders.size();
    }
    for (const auto& [_, orders] : book.asks) {
        total_orders += orders.size();
    }
    EXPECT_EQ(total_orders, 1);

    // Move fair price dramatically to make the order an adverse fill candidate
    if (order_side == OrderSide::BUY) {
        // BUY at order_price is adverse when order_price > fair + threshold
        source_ptr->set_price(
            Price{order_price.value() - config.adverse_fill_threshold.value() - 5});
    } else {
        // SELL at order_price is adverse when order_price + threshold < fair
        source_ptr->set_price(
            Price{order_price.value() + config.adverse_fill_threshold.value() + 5});
    }

    // Run until the next wakeup (at timestamp ~1001) + cancellation processing
    engine.run_until(Timestamp{1100});

    // The original order should be cancelled - check the book
    const auto& book_after = engine.get_order_book(InstrumentID{1});
    bool order_still_exists = false;

    for (const auto& [price, orders] : book_after.bids) {
        for (const auto& o : orders) {
            if (o.order_id == order_id) {
                order_still_exists = true;
            }
        }
    }
    for (const auto& [price, orders] : book_after.asks) {
        for (const auto& o : orders) {
            if (o.order_id == order_id) {
                order_still_exists = true;
            }
        }
    }

    EXPECT_FALSE(order_still_exists);
}
