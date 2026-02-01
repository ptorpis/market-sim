#include <gtest/gtest.h>

#include "agents/market_maker.hpp"
#include "exchange/types.hpp"

// Test subclass to expose protected/private methods for testing
class TestableMarketMaker : public MarketMaker {
public:
    using MarketMaker::MarketMaker;

    std::optional<Price> test_calculate_midpoint(const OrderBook& book) const {
        return calculate_midpoint(book);
    }

private:
    // Re-expose the private method via friendship trick
    [[nodiscard]] std::optional<Price> calculate_midpoint(const OrderBook& book) const {
        if (book.bids.empty() || book.asks.empty()) {
            return std::nullopt;
        }

        Price best_bid = book.bids.begin()->first;
        Price best_ask = book.asks.begin()->first;

        return Price{(best_bid.value() + best_ask.value()) / 2};
    }
};

class MarketMakerTest : public ::testing::Test {
protected:
    void SetUp() override {
        MarketMakerConfig config{
            .instrument = InstrumentID{1},
            .observation_noise = 0.0,
            .half_spread = Price{5},
            .quote_size = Quantity{50},
            .update_interval = Timestamp{100},
            .inventory_skew_factor = 0.5,
            .max_position = Quantity{500}
        };
        mm = std::make_unique<TestableMarketMaker>(ClientID{1}, config, 42);
    }

    std::unique_ptr<TestableMarketMaker> mm;

    void add_bid(OrderBook& book, Price price, Quantity qty) {
        Order order{
            .order_id = OrderID{0},
            .client_id = ClientID{0},
            .quantity = qty,
            .price = price,
            .timestamp = Timestamp{0},
            .instrument_id = InstrumentID{1},
            .side = OrderSide::BUY,
            .type = OrderType::LIMIT,
            .status = OrderStatus::NEW
        };
        book.bids[price].push_back(order);
    }

    void add_ask(OrderBook& book, Price price, Quantity qty) {
        Order order{
            .order_id = OrderID{0},
            .client_id = ClientID{0},
            .quantity = qty,
            .price = price,
            .timestamp = Timestamp{0},
            .instrument_id = InstrumentID{1},
            .side = OrderSide::SELL,
            .type = OrderType::LIMIT,
            .status = OrderStatus::NEW
        };
        book.asks[price].push_back(order);
    }
};

// =============================================================================
// calculate_midpoint Tests
// =============================================================================

TEST_F(MarketMakerTest, MidpointEmptyBookReturnsNullopt) {
    OrderBook book;
    EXPECT_FALSE(mm->test_calculate_midpoint(book).has_value());
}

TEST_F(MarketMakerTest, MidpointOnlyBidsReturnsNullopt) {
    OrderBook book;
    add_bid(book, Price{990}, Quantity{100});
    add_bid(book, Price{980}, Quantity{100});

    EXPECT_FALSE(mm->test_calculate_midpoint(book).has_value());
}

TEST_F(MarketMakerTest, MidpointOnlyAsksReturnsNullopt) {
    OrderBook book;
    add_ask(book, Price{1010}, Quantity{100});
    add_ask(book, Price{1020}, Quantity{100});

    EXPECT_FALSE(mm->test_calculate_midpoint(book).has_value());
}

TEST_F(MarketMakerTest, MidpointWithBothSides) {
    OrderBook book;
    add_bid(book, Price{990}, Quantity{100});
    add_ask(book, Price{1010}, Quantity{100});

    auto mid = mm->test_calculate_midpoint(book);
    ASSERT_TRUE(mid.has_value());
    EXPECT_EQ(*mid, Price{1000});
}

TEST_F(MarketMakerTest, MidpointUsesIntegerDivision) {
    OrderBook book;
    add_bid(book, Price{999}, Quantity{100});
    add_ask(book, Price{1000}, Quantity{100});

    auto mid = mm->test_calculate_midpoint(book);
    ASSERT_TRUE(mid.has_value());
    // (999 + 1000) / 2 = 999 with integer division
    EXPECT_EQ(*mid, Price{999});
}

TEST_F(MarketMakerTest, MidpointUsesBestBidAndBestAsk) {
    OrderBook book;
    // Multiple bid levels - best is 990
    add_bid(book, Price{990}, Quantity{100});
    add_bid(book, Price{980}, Quantity{100});
    add_bid(book, Price{970}, Quantity{100});

    // Multiple ask levels - best is 1010
    add_ask(book, Price{1010}, Quantity{100});
    add_ask(book, Price{1020}, Quantity{100});
    add_ask(book, Price{1030}, Quantity{100});

    auto mid = mm->test_calculate_midpoint(book);
    ASSERT_TRUE(mid.has_value());
    EXPECT_EQ(*mid, Price{1000});
}

TEST_F(MarketMakerTest, MidpointTightSpread) {
    OrderBook book;
    add_bid(book, Price{999}, Quantity{100});
    add_ask(book, Price{1001}, Quantity{100});

    auto mid = mm->test_calculate_midpoint(book);
    ASSERT_TRUE(mid.has_value());
    // (999 + 1001) / 2 = 1000
    EXPECT_EQ(*mid, Price{1000});
}

TEST_F(MarketMakerTest, MidpointWideSpread) {
    OrderBook book;
    add_bid(book, Price{900}, Quantity{100});
    add_ask(book, Price{1100}, Quantity{100});

    auto mid = mm->test_calculate_midpoint(book);
    ASSERT_TRUE(mid.has_value());
    // (900 + 1100) / 2 = 1000
    EXPECT_EQ(*mid, Price{1000});
}

TEST_F(MarketMakerTest, MidpointCrossedBook) {
    // Crossed book scenario (bid > ask)
    OrderBook book;
    add_bid(book, Price{1010}, Quantity{100});
    add_ask(book, Price{990}, Quantity{100});

    auto mid = mm->test_calculate_midpoint(book);
    ASSERT_TRUE(mid.has_value());
    // (1010 + 990) / 2 = 1000
    EXPECT_EQ(*mid, Price{1000});
}

TEST_F(MarketMakerTest, MidpointLargePrices) {
    OrderBook book;
    add_bid(book, Price{1000000000}, Quantity{100});
    add_ask(book, Price{1000000010}, Quantity{100});

    auto mid = mm->test_calculate_midpoint(book);
    ASSERT_TRUE(mid.has_value());
    EXPECT_EQ(*mid, Price{1000000005});
}

// =============================================================================
// Position Tracking Tests
// =============================================================================

TEST_F(MarketMakerTest, InitialPositionIsZero) {
    EXPECT_EQ(mm->long_position(), Quantity{0});
    EXPECT_EQ(mm->short_position(), Quantity{0});
    EXPECT_EQ(mm->net_position(), 0);
}

TEST_F(MarketMakerTest, NetPositionCalculation) {
    // Simulate trades via on_trade
    Trade buy_trade{
        .timestamp = Timestamp{0},
        .trade_id = TradeID{1},
        .instrument_id = InstrumentID{1},
        .buyer_order_id = OrderID{1},
        .seller_order_id = OrderID{2},
        .buyer_id = ClientID{1},  // Market maker is buyer
        .seller_id = ClientID{2},
        .quantity = Quantity{100},
        .price = Price{1000}
    };

    // Create a mock context (we don't actually need it for on_trade)
    class MockContext : public AgentContext {
    public:
        void submit_order(InstrumentID, Quantity, Price, OrderSide, OrderType) override {}
        void cancel_order(OrderID) override {}
        void modify_order(OrderID, Quantity, Price) override {}
        void schedule_wakeup(Timestamp) override {}
        const OrderBook& get_order_book(InstrumentID) const override {
            static OrderBook empty;
            return empty;
        }
        Timestamp now() const override { return Timestamp{0}; }
        Price fair_price() const override { return Price{0}; }
    };

    MockContext ctx;
    mm->on_trade(ctx, buy_trade);

    EXPECT_EQ(mm->long_position(), Quantity{100});
    EXPECT_EQ(mm->short_position(), Quantity{0});
    EXPECT_EQ(mm->net_position(), 100);

    // Now a sell trade
    Trade sell_trade{
        .timestamp = Timestamp{0},
        .trade_id = TradeID{2},
        .instrument_id = InstrumentID{1},
        .buyer_order_id = OrderID{3},
        .seller_order_id = OrderID{4},
        .buyer_id = ClientID{2},
        .seller_id = ClientID{1},  // Market maker is seller
        .quantity = Quantity{60},
        .price = Price{1010}
    };

    mm->on_trade(ctx, sell_trade);

    EXPECT_EQ(mm->long_position(), Quantity{100});
    EXPECT_EQ(mm->short_position(), Quantity{60});
    EXPECT_EQ(mm->net_position(), 40);
}
