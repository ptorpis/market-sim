#include <gtest/gtest.h>

#include "simulation/simulation_engine.hpp"

// =============================================================================
// PnL Struct Unit Tests
// =============================================================================

TEST(PnLStructTest, DefaultConstruction) {
    PnL pnl;
    EXPECT_EQ(pnl.long_position, Quantity{0});
    EXPECT_EQ(pnl.short_position, Quantity{0});
    EXPECT_EQ(pnl.cash, Cash{0});
}

TEST(PnLStructTest, NetPositionLongOnly) {
    PnL pnl{
        .long_position = Quantity{100}, .short_position = Quantity{0}, .cash = Cash{0}};
    EXPECT_EQ(pnl.net_position(), 100);
}

TEST(PnLStructTest, NetPositionShortOnly) {
    PnL pnl{
        .long_position = Quantity{0}, .short_position = Quantity{100}, .cash = Cash{0}};
    EXPECT_EQ(pnl.net_position(), -100);
}

TEST(PnLStructTest, NetPositionMixedLongDominant) {
    PnL pnl{
        .long_position = Quantity{150}, .short_position = Quantity{50}, .cash = Cash{0}};
    EXPECT_EQ(pnl.net_position(), 100);
}

TEST(PnLStructTest, NetPositionMixedShortDominant) {
    PnL pnl{
        .long_position = Quantity{50}, .short_position = Quantity{150}, .cash = Cash{0}};
    EXPECT_EQ(pnl.net_position(), -100);
}

TEST(PnLStructTest, NetPositionFlat) {
    PnL pnl{
        .long_position = Quantity{100}, .short_position = Quantity{100}, .cash = Cash{0}};
    EXPECT_EQ(pnl.net_position(), 0);
}

TEST(PnLStructTest, NetPositionZero) {
    PnL pnl{.long_position = Quantity{0}, .short_position = Quantity{0}, .cash = Cash{0}};
    EXPECT_EQ(pnl.net_position(), 0);
}

// =============================================================================
// Unrealized PnL Tests
// =============================================================================

TEST(PnLStructTest, UnrealizedPnLLongPositionPriceUp) {
    PnL pnl{
        .long_position = Quantity{100}, .short_position = Quantity{0}, .cash = Cash{0}};
    // 100 long @ mark 1100 = 110000
    EXPECT_EQ(pnl.unrealized_pnl(Price{1100}), 110000);
}

TEST(PnLStructTest, UnrealizedPnLLongPositionPriceDown) {
    PnL pnl{
        .long_position = Quantity{100}, .short_position = Quantity{0}, .cash = Cash{0}};
    // 100 long @ mark 900 = 90000
    EXPECT_EQ(pnl.unrealized_pnl(Price{900}), 90000);
}

TEST(PnLStructTest, UnrealizedPnLShortPosition) {
    PnL pnl{
        .long_position = Quantity{0}, .short_position = Quantity{100}, .cash = Cash{0}};
    // net = -100, @ mark 1000 = -100000 (liability)
    EXPECT_EQ(pnl.unrealized_pnl(Price{1000}), -100000);
}

TEST(PnLStructTest, UnrealizedPnLFlatPosition) {
    PnL pnl{
        .long_position = Quantity{50}, .short_position = Quantity{50}, .cash = Cash{0}};
    // net = 0, any mark price = 0
    EXPECT_EQ(pnl.unrealized_pnl(Price{1000}), 0);
    EXPECT_EQ(pnl.unrealized_pnl(Price{5000}), 0);
}

TEST(PnLStructTest, UnrealizedPnLZeroMarkPrice) {
    PnL pnl{
        .long_position = Quantity{100}, .short_position = Quantity{0}, .cash = Cash{0}};
    EXPECT_EQ(pnl.unrealized_pnl(Price{0}), 0);
}

// =============================================================================
// Total PnL Tests
// =============================================================================

TEST(PnLStructTest, TotalPnLBreakeven) {
    // Bought 100 @ 1000, mark at 1000 = breakeven
    PnL pnl{.long_position = Quantity{100},
            .short_position = Quantity{0},
            .cash = Cash{-100000}};
    EXPECT_EQ(pnl.total_pnl(Price{1000}), 0);
}

TEST(PnLStructTest, TotalPnLProfit) {
    // Bought 100 @ 1000, mark at 1100 = 10000 profit
    PnL pnl{.long_position = Quantity{100},
            .short_position = Quantity{0},
            .cash = Cash{-100000}};
    EXPECT_EQ(pnl.total_pnl(Price{1100}), 10000);
}

TEST(PnLStructTest, TotalPnLLoss) {
    // Bought 100 @ 1000, mark at 900 = -10000 loss
    PnL pnl{.long_position = Quantity{100},
            .short_position = Quantity{0},
            .cash = Cash{-100000}};
    EXPECT_EQ(pnl.total_pnl(Price{900}), -10000);
}

TEST(PnLStructTest, TotalPnLShortProfit) {
    // Sold 100 @ 1000, mark at 900 = 10000 profit (bought back cheaper)
    PnL pnl{.long_position = Quantity{0},
            .short_position = Quantity{100},
            .cash = Cash{100000}};
    // cash = 100000, unrealized = -100 * 900 = -90000
    // total = 100000 - 90000 = 10000
    EXPECT_EQ(pnl.total_pnl(Price{900}), 10000);
}

TEST(PnLStructTest, TotalPnLShortLoss) {
    // Sold 100 @ 1000, mark at 1100 = -10000 loss
    PnL pnl{.long_position = Quantity{0},
            .short_position = Quantity{100},
            .cash = Cash{100000}};
    // cash = 100000, unrealized = -100 * 1100 = -110000
    // total = 100000 - 110000 = -10000
    EXPECT_EQ(pnl.total_pnl(Price{1100}), -10000);
}

TEST(PnLStructTest, TotalPnLFlatWithProfit) {
    // Long 100 @ 1000, short 100 @ 1050 = locked in 5000 profit
    PnL pnl{
        .long_position = Quantity{100},
        .short_position = Quantity{100},
        .cash = Cash{-100000 + 105000} // net cash = 5000
    };
    // net position = 0, unrealized = 0
    // total = 5000 + 0 = 5000
    EXPECT_EQ(pnl.total_pnl(Price{1000}), 5000);
    EXPECT_EQ(pnl.total_pnl(Price{2000}), 5000); // Any mark price, same result
}

TEST(PnLStructTest, TotalPnLZeroEverything) {
    PnL pnl;
    EXPECT_EQ(pnl.total_pnl(Price{1000}), 0);
    EXPECT_EQ(pnl.total_pnl(Price{0}), 0);
}

// =============================================================================
// Large Value Tests
// =============================================================================

TEST(PnLStructTest, LargePositionNetPosition) {
    PnL pnl{.long_position = Quantity{1000000000}, // 1 billion
            .short_position = Quantity{500000000}, // 500 million
            .cash = Cash{0}};
    EXPECT_EQ(pnl.net_position(), 500000000);
}

TEST(PnLStructTest, LargePositionUnrealizedPnL) {
    PnL pnl{.long_position = Quantity{1000000}, // 1 million
            .short_position = Quantity{0},
            .cash = Cash{0}};
    // 1 million @ 50000 = 50 billion
    EXPECT_EQ(pnl.unrealized_pnl(Price{50000}), 50000000000LL);
}

TEST(PnLStructTest, LargeCashValue) {
    PnL pnl{
        .long_position = Quantity{0},
        .short_position = Quantity{0},
        .cash = Cash{1000000000000LL} // 1 trillion
    };
    EXPECT_EQ(pnl.total_pnl(Price{1000}), 1000000000000LL);
}

TEST(PnLStructTest, NegativeCash) {
    PnL pnl{
        .long_position = Quantity{100},
        .short_position = Quantity{0},
        .cash = Cash{-500000} // Negative cash (spent more)
    };
    EXPECT_EQ(pnl.cash.value(), -500000);
    // At mark 1000: -500000 + 100*1000 = -400000
    EXPECT_EQ(pnl.total_pnl(Price{1000}), -400000);
}
