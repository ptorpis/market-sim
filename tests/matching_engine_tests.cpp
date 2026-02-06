#include <gtest/gtest.h>
#include <tuple>

#include "exchange/matching_engine.hpp"
#include "exchange/types.hpp"

class MatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override { engine = std::make_unique<MatchingEngine>(InstrumentID{1}); }

    void TearDown() override { engine.reset(); }

    std::unique_ptr<MatchingEngine> engine;

    // Helper to create a limit buy order request
    OrderRequest make_limit_buy(ClientID client, Quantity qty, Price price) {
        return OrderRequest{.client_id = client,
                            .quantity = qty,
                            .price = price,
                            .instrument_id = InstrumentID{1},
                            .side = OrderSide::BUY,
                            .type = OrderType::LIMIT};
    }

    // Helper to create a limit sell order request
    OrderRequest make_limit_sell(ClientID client, Quantity qty, Price price) {
        return OrderRequest{.client_id = client,
                            .quantity = qty,
                            .price = price,
                            .instrument_id = InstrumentID{1},
                            .side = OrderSide::SELL,
                            .type = OrderType::LIMIT};
    }

    // Helper to create a market buy order request
    OrderRequest make_market_buy(ClientID client, Quantity qty) {
        return OrderRequest{.client_id = client,
                            .quantity = qty,
                            .price = Price{0},
                            .instrument_id = InstrumentID{1},
                            .side = OrderSide::BUY,
                            .type = OrderType::MARKET};
    }

    // Helper to create a market sell order request
    OrderRequest make_market_sell(ClientID client, Quantity qty) {
        return OrderRequest{.client_id = client,
                            .quantity = qty,
                            .price = Price{0},
                            .instrument_id = InstrumentID{1},
                            .side = OrderSide::SELL,
                            .type = OrderType::MARKET};
    }
};

// =============================================================================
// Basic Order Processing Tests
// =============================================================================

TEST_F(MatchingEngineTest, LimitBuyOrderAddedToEmptyBook) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::NEW);
    EXPECT_EQ(result.remaining_quantity, Quantity{100});
    EXPECT_EQ(result.accepted_price, Price{1000});
    EXPECT_TRUE(result.trade_vec.empty());
    EXPECT_EQ(result.order_id, OrderID{1});
}

TEST_F(MatchingEngineTest, LimitSellOrderAddedToEmptyBook) {
    auto result =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{50}, Price{1500}));

    EXPECT_EQ(result.status, OrderStatus::NEW);
    EXPECT_EQ(result.remaining_quantity, Quantity{50});
    EXPECT_EQ(result.accepted_price, Price{1500});
    EXPECT_TRUE(result.trade_vec.empty());
    EXPECT_EQ(result.order_id, OrderID{1});
}

TEST_F(MatchingEngineTest, MarketBuyOrderOnEmptyBookIsCancelled) {
    auto result = engine->process_order(make_market_buy(ClientID{1}, Quantity{100}));

    EXPECT_EQ(result.status, OrderStatus::CANCELLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{100});
    EXPECT_TRUE(result.trade_vec.empty());
}

TEST_F(MatchingEngineTest, MarketSellOrderOnEmptyBookIsCancelled) {
    auto result = engine->process_order(make_market_sell(ClientID{1}, Quantity{100}));

    EXPECT_EQ(result.status, OrderStatus::CANCELLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{100});
    EXPECT_TRUE(result.trade_vec.empty());
}

// =============================================================================
// Order Matching Tests - Full Fills
// =============================================================================

TEST_F(MatchingEngineTest, BuyOrderFullyMatchesSellOrder) {
    // First, add a sell order to the book
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1000}));

    // Then, send a buy order that should match
    auto result =
        engine->process_order(make_limit_buy(ClientID{2}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{0});
    EXPECT_EQ(result.trade_vec.size(), 1);

    const auto& trade = result.trade_vec[0];
    EXPECT_EQ(trade.quantity, Quantity{100});
    EXPECT_EQ(trade.price, Price{1000});
    EXPECT_EQ(trade.buyer_id, ClientID{2});
    EXPECT_EQ(trade.seller_id, ClientID{1});
}

TEST_F(MatchingEngineTest, SellOrderFullyMatchesBuyOrder) {
    // First, add a buy order to the book
    std::ignore =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    // Then, send a sell order that should match
    auto result =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{0});
    EXPECT_EQ(result.trade_vec.size(), 1);

    const auto& trade = result.trade_vec[0];
    EXPECT_EQ(trade.quantity, Quantity{100});
    EXPECT_EQ(trade.price, Price{1000});
    EXPECT_EQ(trade.buyer_id, ClientID{1});
    EXPECT_EQ(trade.seller_id, ClientID{2});
}

TEST_F(MatchingEngineTest, BuyOrderMatchesAtBetterPrice) {
    // Sell at 900
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{900}));

    // Buy at 1000 - should match at 900 (the resting order's price)
    auto result =
        engine->process_order(make_limit_buy(ClientID{2}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.trade_vec.size(), 1);
    EXPECT_EQ(result.trade_vec[0].price, Price{900});
}

// =============================================================================
// Order Matching Tests - Partial Fills
// =============================================================================

TEST_F(MatchingEngineTest, BuyOrderPartiallyFilled) {
    // Add sell order for 50 units
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{50}, Price{1000}));

    // Buy order for 100 units - only 50 will match
    auto result =
        engine->process_order(make_limit_buy(ClientID{2}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{50});
    EXPECT_EQ(result.trade_vec.size(), 1);
    EXPECT_EQ(result.trade_vec[0].quantity, Quantity{50});
}

TEST_F(MatchingEngineTest, SellOrderPartiallyFilled) {
    // Add buy order for 50 units
    std::ignore =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{50}, Price{1000}));

    // Sell order for 100 units - only 50 will match
    auto result =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{50});
    EXPECT_EQ(result.trade_vec.size(), 1);
    EXPECT_EQ(result.trade_vec[0].quantity, Quantity{50});
}

TEST_F(MatchingEngineTest, IncomingOrderFillsMultipleRestingOrders) {
    // Add multiple sell orders
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{30}, Price{1000}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{40}, Price{1000}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{3}, Quantity{30}, Price{1000}));

    // Buy order that matches all three
    auto result =
        engine->process_order(make_limit_buy(ClientID{4}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{0});
    EXPECT_EQ(result.trade_vec.size(), 3);
    EXPECT_EQ(result.trade_vec[0].quantity, Quantity{30});
    EXPECT_EQ(result.trade_vec[1].quantity, Quantity{40});
    EXPECT_EQ(result.trade_vec[2].quantity, Quantity{30});
}

// =============================================================================
// Price Priority Tests
// =============================================================================

TEST_F(MatchingEngineTest, BuyOrderMatchesBestAskFirst) {
    // Add sells at different prices
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{50}, Price{1100}));
    std::ignore = engine->process_order(
        make_limit_sell(ClientID{2}, Quantity{50}, Price{1000})); // Best ask
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{3}, Quantity{50}, Price{1050}));

    // Buy order should match at best ask (1000) first
    auto result =
        engine->process_order(make_limit_buy(ClientID{4}, Quantity{50}, Price{1100}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.trade_vec.size(), 1);
    EXPECT_EQ(result.trade_vec[0].price, Price{1000});
    EXPECT_EQ(result.trade_vec[0].seller_id, ClientID{2});
}

TEST_F(MatchingEngineTest, SellOrderMatchesBestBidFirst) {
    // Add buys at different prices
    std::ignore =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{50}, Price{900}));
    std::ignore = engine->process_order(
        make_limit_buy(ClientID{2}, Quantity{50}, Price{1000})); // Best bid
    std::ignore =
        engine->process_order(make_limit_buy(ClientID{3}, Quantity{50}, Price{950}));

    // Sell order should match at best bid (1000) first
    auto result =
        engine->process_order(make_limit_sell(ClientID{4}, Quantity{50}, Price{900}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.trade_vec.size(), 1);
    EXPECT_EQ(result.trade_vec[0].price, Price{1000});
    EXPECT_EQ(result.trade_vec[0].buyer_id, ClientID{2});
}

TEST_F(MatchingEngineTest, BuyOrderSweepsMultiplePriceLevels) {
    // Add sells at different prices
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{30}, Price{1000}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{30}, Price{1010}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{3}, Quantity{30}, Price{1020}));

    // Buy order that sweeps all levels
    auto result =
        engine->process_order(make_limit_buy(ClientID{4}, Quantity{90}, Price{1020}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{0});
    EXPECT_EQ(result.trade_vec.size(), 3);
    // Should match lowest price first
    EXPECT_EQ(result.trade_vec[0].price, Price{1000});
    EXPECT_EQ(result.trade_vec[1].price, Price{1010});
    EXPECT_EQ(result.trade_vec[2].price, Price{1020});
}

// =============================================================================
// Time Priority (FIFO) Tests
// =============================================================================

TEST_F(MatchingEngineTest, OrdersAtSamePriceLevelMatchFIFO) {
    // Add multiple sells at the same price
    std::ignore = engine->process_order(
        make_limit_sell(ClientID{1}, Quantity{30}, Price{1000})); // First
    std::ignore = engine->process_order(
        make_limit_sell(ClientID{2}, Quantity{30}, Price{1000})); // Second
    std::ignore = engine->process_order(
        make_limit_sell(ClientID{3}, Quantity{30}, Price{1000})); // Third

    // Buy enough to match first two
    auto result =
        engine->process_order(make_limit_buy(ClientID{4}, Quantity{60}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.trade_vec.size(), 2);
    // Should match in order of arrival
    EXPECT_EQ(result.trade_vec[0].seller_id, ClientID{1});
    EXPECT_EQ(result.trade_vec[1].seller_id, ClientID{2});
}

// =============================================================================
// Self-Trade Prevention Tests
// =============================================================================

TEST_F(MatchingEngineTest, SelfTradePreventedSameClient) {
    // Add a sell order
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1000}));

    // Same client tries to buy - should not match
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    // Order should be added to book, not matched
    EXPECT_EQ(result.status, OrderStatus::NEW);
    EXPECT_EQ(result.remaining_quantity, Quantity{100});
    EXPECT_TRUE(result.trade_vec.empty());
}

TEST_F(MatchingEngineTest, SelfTradeSkipsToNextOrder) {
    // Add orders from multiple clients
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{50}, Price{1000}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{50}, Price{1000}));

    // Client 1 buys - should skip their own order and match with client 2
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{50}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.trade_vec.size(), 1);
    EXPECT_EQ(result.trade_vec[0].seller_id, ClientID{2});
}

// =============================================================================
// Market Order Tests
// =============================================================================

TEST_F(MatchingEngineTest, MarketBuyOrderFillsCompletely) {
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1000}));

    auto result = engine->process_order(make_market_buy(ClientID{2}, Quantity{100}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{0});
    EXPECT_EQ(result.trade_vec.size(), 1);
}

TEST_F(MatchingEngineTest, MarketSellOrderFillsCompletely) {
    std::ignore =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    auto result = engine->process_order(make_market_sell(ClientID{2}, Quantity{100}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{0});
    EXPECT_EQ(result.trade_vec.size(), 1);
}

TEST_F(MatchingEngineTest, MarketOrderPartialFillThenCancelled) {
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{50}, Price{1000}));

    auto result = engine->process_order(make_market_buy(ClientID{2}, Quantity{100}));

    EXPECT_EQ(result.status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_EQ(result.remaining_quantity, Quantity{50});
    EXPECT_EQ(result.trade_vec.size(), 1);
}

TEST_F(MatchingEngineTest, MarketOrderSweepsMultiplePriceLevels) {
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{30}, Price{1000}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{30}, Price{2000}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{3}, Quantity{30}, Price{3000}));

    // Market order ignores price - should sweep all levels
    auto result = engine->process_order(make_market_buy(ClientID{4}, Quantity{90}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.trade_vec.size(), 3);
}

TEST_F(MatchingEngineTest, MarketOrderNotAddedToBook) {
    // Add some liquidity
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{50}, Price{1000}));

    // Market buy that can't be fully filled
    auto result = engine->process_order(make_market_buy(ClientID{2}, Quantity{100}));

    // Check the unfilled portion is not in the book
    auto order = engine->get_order(result.order_id);
    EXPECT_FALSE(order.has_value());
}

// =============================================================================
// Price Check Tests for Limit Orders
// =============================================================================

TEST_F(MatchingEngineTest, LimitBuyDoesNotMatchHigherAsk) {
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1100}));

    // Buy at 1000 should not match sell at 1100
    auto result =
        engine->process_order(make_limit_buy(ClientID{2}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::NEW);
    EXPECT_EQ(result.remaining_quantity, Quantity{100});
    EXPECT_TRUE(result.trade_vec.empty());
}

TEST_F(MatchingEngineTest, LimitSellDoesNotMatchLowerBid) {
    std::ignore =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{900}));

    // Sell at 1000 should not match buy at 900
    auto result =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{100}, Price{1000}));

    EXPECT_EQ(result.status, OrderStatus::NEW);
    EXPECT_EQ(result.remaining_quantity, Quantity{100});
    EXPECT_TRUE(result.trade_vec.empty());
}

// =============================================================================
// Order Lookup Tests
// =============================================================================

TEST_F(MatchingEngineTest, GetExistingOrder) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    auto order = engine->get_order(result.order_id);

    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->order_id, result.order_id);
    EXPECT_EQ(order->client_id, ClientID{1});
    EXPECT_EQ(order->quantity, Quantity{100});
    EXPECT_EQ(order->price, Price{1000});
    EXPECT_EQ(order->side, OrderSide::BUY);
}

TEST_F(MatchingEngineTest, GetNonExistentOrder) {
    auto order = engine->get_order(OrderID{999});

    EXPECT_FALSE(order.has_value());
}

TEST_F(MatchingEngineTest, GetFilledOrderNotInBook) {
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1000}));
    auto result =
        engine->process_order(make_limit_buy(ClientID{2}, Quantity{100}, Price{1000}));

    // The sell order was fully filled and removed
    auto order = engine->get_order(OrderID{1});
    EXPECT_FALSE(order.has_value());

    // The buy order was also fully filled and not added to book
    auto buyOrder = engine->get_order(result.order_id);
    EXPECT_FALSE(buyOrder.has_value());
}

// =============================================================================
// Order Cancellation Tests
// =============================================================================

TEST_F(MatchingEngineTest, CancelExistingOrder) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    bool cancelled = engine->cancel_order(ClientID{1}, result.order_id);

    EXPECT_TRUE(cancelled);

    // Verify order is no longer in book
    auto order = engine->get_order(result.order_id);
    EXPECT_FALSE(order.has_value());
}

TEST_F(MatchingEngineTest, CancelNonExistentOrder) {
    bool cancelled = engine->cancel_order(ClientID{1}, OrderID{999});

    EXPECT_FALSE(cancelled);
}

TEST_F(MatchingEngineTest, CancelOrderWithWrongClientId) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    // Try to cancel with different client
    bool cancelled = engine->cancel_order(ClientID{2}, result.order_id);

    EXPECT_FALSE(cancelled);

    // Order should still be in book
    auto order = engine->get_order(result.order_id);
    EXPECT_TRUE(order.has_value());
}

TEST_F(MatchingEngineTest, CancelSellOrder) {
    auto result =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1000}));

    bool cancelled = engine->cancel_order(ClientID{1}, result.order_id);

    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(engine->get_order(result.order_id).has_value());
}

// =============================================================================
// Order Modification Tests
// =============================================================================

TEST_F(MatchingEngineTest, ModifyOrderQuantityDown) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    auto modResult =
        engine->modify_order(ClientID{1}, result.order_id, Quantity{50}, Price{1000});

    EXPECT_EQ(modResult.status, ModifyStatus::ACCEPTED);
    EXPECT_EQ(modResult.old_order_id, result.order_id);
    EXPECT_EQ(modResult.new_order_id,
              result.order_id); // Same ID when qty down, same price
    EXPECT_FALSE(modResult.match_result.has_value());

    auto order = engine->get_order(result.order_id);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->quantity, Quantity{50});
    EXPECT_EQ(order->status, OrderStatus::MODIFIED);
}

TEST_F(MatchingEngineTest, ModifyOrderNoChange) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    auto modResult =
        engine->modify_order(ClientID{1}, result.order_id, Quantity{100}, Price{1000});

    EXPECT_EQ(modResult.status, ModifyStatus::ACCEPTED);
    EXPECT_EQ(modResult.old_order_id, result.order_id);
    EXPECT_EQ(modResult.new_order_id, result.order_id);
    EXPECT_FALSE(modResult.match_result.has_value());
}

TEST_F(MatchingEngineTest, ModifyOrderPrice) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    auto modResult =
        engine->modify_order(ClientID{1}, result.order_id, Quantity{100}, Price{1100});

    EXPECT_EQ(modResult.status, ModifyStatus::ACCEPTED);
    EXPECT_EQ(modResult.old_order_id, result.order_id);
    EXPECT_NE(modResult.new_order_id, result.order_id); // New ID for price change
    EXPECT_TRUE(modResult.match_result.has_value());

    // Old order should be gone
    EXPECT_FALSE(engine->get_order(result.order_id).has_value());

    // New order should exist
    auto newOrder = engine->get_order(modResult.new_order_id);
    ASSERT_TRUE(newOrder.has_value());
    EXPECT_EQ(newOrder->price, Price{1100});
}

TEST_F(MatchingEngineTest, ModifyOrderQuantityUp) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    auto modResult =
        engine->modify_order(ClientID{1}, result.order_id, Quantity{150}, Price{1000});

    EXPECT_EQ(modResult.status, ModifyStatus::ACCEPTED);
    // Quantity up requires cancel and re-submit (loses priority)
    EXPECT_NE(modResult.new_order_id, result.order_id);
    EXPECT_TRUE(modResult.match_result.has_value());
}

TEST_F(MatchingEngineTest, ModifyNonExistentOrder) {
    auto modResult =
        engine->modify_order(ClientID{1}, OrderID{999}, Quantity{100}, Price{1000});

    EXPECT_EQ(modResult.status, ModifyStatus::INVALID);
}

TEST_F(MatchingEngineTest, ModifyOrderWithWrongClientId) {
    auto result =
        engine->process_order(make_limit_buy(ClientID{1}, Quantity{100}, Price{1000}));

    auto modResult =
        engine->modify_order(ClientID{2}, result.order_id, Quantity{50}, Price{1000});

    EXPECT_EQ(modResult.status, ModifyStatus::INVALID);

    // Order should be unchanged
    auto order = engine->get_order(result.order_id);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->quantity, Quantity{100});
}

TEST_F(MatchingEngineTest, ModifyOrderTriggersMatch) {
    // Add a sell order
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1000}));

    // Add a buy order at a lower price
    auto buyResult =
        engine->process_order(make_limit_buy(ClientID{2}, Quantity{100}, Price{900}));

    // Modify buy to match the sell
    auto modResult =
        engine->modify_order(ClientID{2}, buyResult.order_id, Quantity{100}, Price{1000});

    EXPECT_EQ(modResult.status, ModifyStatus::ACCEPTED);
    ASSERT_TRUE(modResult.match_result.has_value());
    EXPECT_EQ(modResult.match_result->status, OrderStatus::FILLED);
    EXPECT_EQ(modResult.match_result->trade_vec.size(), 1);
}

// =============================================================================
// Trade Event Verification Tests
// =============================================================================

TEST_F(MatchingEngineTest, TradeEventHasCorrectBuyerAndSeller) {
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{10}, Quantity{100}, Price{1000}));
    auto result =
        engine->process_order(make_limit_buy(ClientID{20}, Quantity{100}, Price{1000}));

    ASSERT_EQ(result.trade_vec.size(), 1);
    const auto& trade = result.trade_vec[0];

    EXPECT_EQ(trade.buyer_id, ClientID{20});
    EXPECT_EQ(trade.seller_id, ClientID{10});
    EXPECT_EQ(trade.buyer_order_id, result.order_id);
    EXPECT_EQ(trade.seller_order_id, OrderID{1});
    EXPECT_EQ(trade.aggressor_side, OrderSide::BUY);
}

TEST_F(MatchingEngineTest, TradeEventHasCorrectInstrumentId) {
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1000}));
    auto result =
        engine->process_order(make_limit_buy(ClientID{2}, Quantity{100}, Price{1000}));

    ASSERT_EQ(result.trade_vec.size(), 1);
    EXPECT_EQ(result.trade_vec[0].instrument_id, InstrumentID{1});
}

TEST_F(MatchingEngineTest, MultipleTradesHaveUniqueTradeIds) {
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{50}, Price{1000}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{50}, Price{1000}));

    auto result =
        engine->process_order(make_limit_buy(ClientID{3}, Quantity{100}, Price{1000}));

    ASSERT_EQ(result.trade_vec.size(), 2);
    EXPECT_NE(result.trade_vec[0].trade_id, result.trade_vec[1].trade_id);
}

// =============================================================================
// Complex Scenario Tests
// =============================================================================

TEST_F(MatchingEngineTest, ComplexOrderBookScenario) {
    // Build up an order book
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1020}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{100}, Price{1010}));
    std::ignore =
        engine->process_order(make_limit_sell(ClientID{3}, Quantity{100}, Price{1000}));

    std::ignore =
        engine->process_order(make_limit_buy(ClientID{4}, Quantity{100}, Price{990}));
    std::ignore =
        engine->process_order(make_limit_buy(ClientID{5}, Quantity{100}, Price{980}));
    std::ignore =
        engine->process_order(make_limit_buy(ClientID{6}, Quantity{100}, Price{970}));

    // Aggressive buy that sweeps part of the asks (100 @ 1000 + 50 @ 1010 = 150 total)
    auto result =
        engine->process_order(make_limit_buy(ClientID{7}, Quantity{150}, Price{1010}));

    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_EQ(result.trade_vec.size(), 2); // Matched with sells at 1000 and 1010
    EXPECT_EQ(result.remaining_quantity, Quantity{0});

    // Order was fully filled, so not in book
    auto order = engine->get_order(result.order_id);
    EXPECT_FALSE(order.has_value());
}

TEST_F(MatchingEngineTest, OrderBookAfterMultipleOperations) {
    // Add orders
    auto sell1 =
        engine->process_order(make_limit_sell(ClientID{1}, Quantity{100}, Price{1000}));
    auto sell2 =
        engine->process_order(make_limit_sell(ClientID{2}, Quantity{100}, Price{1000}));
    auto buy1 =
        engine->process_order(make_limit_buy(ClientID{3}, Quantity{100}, Price{900}));

    // Verify orders are in book
    EXPECT_TRUE(engine->get_order(sell1.order_id).has_value());
    EXPECT_TRUE(engine->get_order(sell2.order_id).has_value());
    EXPECT_TRUE(engine->get_order(buy1.order_id).has_value());

    // Cancel one sell
    EXPECT_TRUE(engine->cancel_order(ClientID{1}, sell1.order_id));
    EXPECT_FALSE(engine->get_order(sell1.order_id).has_value());

    // Modify the buy
    std::ignore =
        engine->modify_order(ClientID{3}, buy1.order_id, Quantity{50}, Price{900});
    auto modifiedBuy = engine->get_order(buy1.order_id);
    ASSERT_TRUE(modifiedBuy.has_value());
    EXPECT_EQ(modifiedBuy->quantity, Quantity{50});

    // Match the remaining sell
    auto result =
        engine->process_order(make_limit_buy(ClientID{4}, Quantity{100}, Price{1000}));
    EXPECT_EQ(result.status, OrderStatus::FILLED);
    EXPECT_FALSE(engine->get_order(sell2.order_id).has_value());
}
