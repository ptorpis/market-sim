#include <gtest/gtest.h>

#include "simulation/fair_price.hpp"

class FairPriceTest : public ::testing::Test {
protected:
    FairPriceConfig make_config(Price initial = Price{1'000'000},
                                double drift = 0.0,
                                double volatility = 0.01,
                                Timestamp tick_size = Timestamp{1000}) {
        return FairPriceConfig{
            .initial_price = initial,
            .drift = drift,
            .volatility = volatility,
            .tick_size = tick_size
        };
    }
};

// =============================================================================
// Initial State
// =============================================================================

TEST_F(FairPriceTest, InitialPriceIsCorrect) {
    FairPriceGenerator gen(make_config(Price{1'000'000}), 42);
    EXPECT_EQ(gen.true_price(), Price{1'000'000});
}

TEST_F(FairPriceTest, InitialLastUpdateIsZero) {
    FairPriceGenerator gen(make_config(), 42);
    EXPECT_EQ(gen.last_update(), Timestamp{0});
}

TEST_F(FairPriceTest, ConfigAccessor) {
    auto config = make_config(Price{500'000}, 0.001, 0.02, Timestamp{500});
    FairPriceGenerator gen(config, 42);

    EXPECT_EQ(gen.config().initial_price, Price{500'000});
    EXPECT_DOUBLE_EQ(gen.config().drift, 0.001);
    EXPECT_DOUBLE_EQ(gen.config().volatility, 0.02);
    EXPECT_EQ(gen.config().tick_size, Timestamp{500});
}

// =============================================================================
// Advance Behavior
// =============================================================================

TEST_F(FairPriceTest, AdvanceToZeroDoesNothing) {
    FairPriceGenerator gen(make_config(Price{1'000'000}), 42);
    gen.advance_to(Timestamp{0});

    EXPECT_EQ(gen.true_price(), Price{1'000'000});
    EXPECT_EQ(gen.last_update(), Timestamp{0});
}

TEST_F(FairPriceTest, AdvanceToEarlierTimestampDoesNothing) {
    FairPriceGenerator gen(make_config(Price{1'000'000}), 42);

    gen.advance_to(Timestamp{1000});
    Price price_at_1000 = gen.true_price();

    gen.advance_to(Timestamp{500});

    EXPECT_EQ(gen.true_price(), price_at_1000);
    EXPECT_EQ(gen.last_update(), Timestamp{1000});
}

TEST_F(FairPriceTest, AdvanceToSameTimestampDoesNothing) {
    FairPriceGenerator gen(make_config(Price{1'000'000}), 42);

    gen.advance_to(Timestamp{1000});
    Price first_price = gen.true_price();

    gen.advance_to(Timestamp{1000});
    Price second_price = gen.true_price();

    EXPECT_EQ(first_price, second_price);
}

TEST_F(FairPriceTest, AdvanceUpdatesLastUpdate) {
    FairPriceGenerator gen(make_config(), 42);

    gen.advance_to(Timestamp{500});
    EXPECT_EQ(gen.last_update(), Timestamp{500});

    gen.advance_to(Timestamp{1000});
    EXPECT_EQ(gen.last_update(), Timestamp{1000});
}

TEST_F(FairPriceTest, AdvanceChangesPriceWithVolatility) {
    FairPriceGenerator gen(make_config(Price{1'000'000}, 0.0, 0.05), 42);

    gen.advance_to(Timestamp{1000});

    // With volatility > 0, price should change (extremely unlikely to stay exact)
    EXPECT_NE(gen.true_price(), Price{1'000'000});
}

// =============================================================================
// Deterministic with Same Seed
// =============================================================================

TEST_F(FairPriceTest, SameSeedProducesSamePrices) {
    auto config = make_config(Price{1'000'000}, 0.0001, 0.01);

    FairPriceGenerator gen1(config, 12345);
    FairPriceGenerator gen2(config, 12345);

    gen1.advance_to(Timestamp{1000});
    gen2.advance_to(Timestamp{1000});

    EXPECT_EQ(gen1.true_price(), gen2.true_price());

    gen1.advance_to(Timestamp{2000});
    gen2.advance_to(Timestamp{2000});

    EXPECT_EQ(gen1.true_price(), gen2.true_price());
}

TEST_F(FairPriceTest, DifferentSeedsProduceDifferentPrices) {
    auto config = make_config(Price{1'000'000}, 0.0001, 0.01);

    FairPriceGenerator gen1(config, 12345);
    FairPriceGenerator gen2(config, 54321);

    gen1.advance_to(Timestamp{1000});
    gen2.advance_to(Timestamp{1000});

    // Different seeds should produce different prices (extremely unlikely to match)
    EXPECT_NE(gen1.true_price(), gen2.true_price());
}

// =============================================================================
// Zero Volatility
// =============================================================================

TEST_F(FairPriceTest, ZeroVolatilityWithZeroDriftMaintainsPrice) {
    FairPriceGenerator gen(make_config(Price{1'000'000}, 0.0, 0.0), 42);

    gen.advance_to(Timestamp{1000});
    EXPECT_EQ(gen.true_price(), Price{1'000'000});

    gen.advance_to(Timestamp{5000});
    EXPECT_EQ(gen.true_price(), Price{1'000'000});
}

TEST_F(FairPriceTest, ZeroVolatilityWithPositiveDriftIncreases) {
    FairPriceGenerator gen(make_config(Price{1'000'000}, 0.01, 0.0), 42);

    gen.advance_to(Timestamp{10000}); // 10 ticks with drift 0.01

    // Price should increase with positive drift
    EXPECT_GT(gen.true_price(), Price{1'000'000});
}

// =============================================================================
// Price Rounding
// =============================================================================

TEST_F(FairPriceTest, TruePriceIsRounded) {
    // The true_price() method rounds to nearest integer
    FairPriceGenerator gen(make_config(Price{1'000'000}, 0.0001, 0.001), 42);

    gen.advance_to(Timestamp{500});

    // Just verify it returns a valid Price (internally rounded)
    Price p = gen.true_price();
    EXPECT_GT(p.value(), 0ULL);
}

// =============================================================================
// Multiple Advances
// =============================================================================

TEST_F(FairPriceTest, MultipleAdvancesAccumulateChanges) {
    FairPriceGenerator gen(make_config(Price{1'000'000}, 0.0, 0.02), 42);

    std::vector<Price> prices;
    prices.push_back(gen.true_price());

    for (Timestamp t{1000}; t <= Timestamp{5000}; t += Timestamp{1000}) {
        gen.advance_to(t);
        prices.push_back(gen.true_price());
    }

    // With volatility, we expect some variation in prices
    bool any_different = false;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i] != prices[i - 1]) {
            any_different = true;
            break;
        }
    }
    EXPECT_TRUE(any_different);
}
