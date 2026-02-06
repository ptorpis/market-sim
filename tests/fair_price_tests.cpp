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

// =============================================================================
// Jump Diffusion Tests
// =============================================================================

class JumpDiffusionTest : public ::testing::Test {
protected:
    JumpDiffusionConfig make_config(Price initial = Price{1'000'000},
                                    double drift = 0.0,
                                    double volatility = 0.01,
                                    Timestamp tick_size = Timestamp{1000},
                                    double jump_intensity = 0.1,
                                    double jump_mean = 0.0,
                                    double jump_std = 0.05) {
        return JumpDiffusionConfig{
            .initial_price = initial,
            .drift = drift,
            .volatility = volatility,
            .tick_size = tick_size,
            .jump_intensity = jump_intensity,
            .jump_mean = jump_mean,
            .jump_std = jump_std
        };
    }
};

TEST_F(JumpDiffusionTest, InitialPriceIsCorrect) {
    JumpDiffusionFairPriceGenerator gen(make_config(Price{1'000'000}), 42);
    EXPECT_EQ(gen.true_price(), Price{1'000'000});
}

TEST_F(JumpDiffusionTest, InitialLastUpdateIsZero) {
    JumpDiffusionFairPriceGenerator gen(make_config(), 42);
    EXPECT_EQ(gen.last_update(), Timestamp{0});
}

TEST_F(JumpDiffusionTest, ConfigAccessor) {
    auto config = make_config(Price{500'000}, 0.001, 0.02, Timestamp{500}, 0.2, -0.01, 0.1);
    JumpDiffusionFairPriceGenerator gen(config, 42);

    EXPECT_EQ(gen.config().initial_price, Price{500'000});
    EXPECT_DOUBLE_EQ(gen.config().drift, 0.001);
    EXPECT_DOUBLE_EQ(gen.config().volatility, 0.02);
    EXPECT_EQ(gen.config().tick_size, Timestamp{500});
    EXPECT_DOUBLE_EQ(gen.config().jump_intensity, 0.2);
    EXPECT_DOUBLE_EQ(gen.config().jump_mean, -0.01);
    EXPECT_DOUBLE_EQ(gen.config().jump_std, 0.1);
}

TEST_F(JumpDiffusionTest, AdvanceToZeroDoesNothing) {
    JumpDiffusionFairPriceGenerator gen(make_config(Price{1'000'000}), 42);
    gen.advance_to(Timestamp{0});

    EXPECT_EQ(gen.true_price(), Price{1'000'000});
    EXPECT_EQ(gen.last_update(), Timestamp{0});
}

TEST_F(JumpDiffusionTest, AdvanceToEarlierTimestampDoesNothing) {
    JumpDiffusionFairPriceGenerator gen(make_config(Price{1'000'000}), 42);

    gen.advance_to(Timestamp{1000});
    Price price_at_1000 = gen.true_price();

    gen.advance_to(Timestamp{500});

    EXPECT_EQ(gen.true_price(), price_at_1000);
    EXPECT_EQ(gen.last_update(), Timestamp{1000});
}

TEST_F(JumpDiffusionTest, AdvanceUpdatesLastUpdate) {
    JumpDiffusionFairPriceGenerator gen(make_config(), 42);

    gen.advance_to(Timestamp{500});
    EXPECT_EQ(gen.last_update(), Timestamp{500});

    gen.advance_to(Timestamp{1000});
    EXPECT_EQ(gen.last_update(), Timestamp{1000});
}

TEST_F(JumpDiffusionTest, SameSeedProducesSamePrices) {
    auto config = make_config(Price{1'000'000}, 0.0001, 0.01, Timestamp{1000}, 0.1, 0.0, 0.05);

    JumpDiffusionFairPriceGenerator gen1(config, 12345);
    JumpDiffusionFairPriceGenerator gen2(config, 12345);

    gen1.advance_to(Timestamp{1000});
    gen2.advance_to(Timestamp{1000});

    EXPECT_EQ(gen1.true_price(), gen2.true_price());

    gen1.advance_to(Timestamp{2000});
    gen2.advance_to(Timestamp{2000});

    EXPECT_EQ(gen1.true_price(), gen2.true_price());
}

TEST_F(JumpDiffusionTest, DifferentSeedsProduceDifferentPrices) {
    auto config = make_config(Price{1'000'000}, 0.0001, 0.01, Timestamp{1000}, 0.1, 0.0, 0.05);

    JumpDiffusionFairPriceGenerator gen1(config, 12345);
    JumpDiffusionFairPriceGenerator gen2(config, 54321);

    gen1.advance_to(Timestamp{1000});
    gen2.advance_to(Timestamp{1000});

    EXPECT_NE(gen1.true_price(), gen2.true_price());
}

TEST_F(JumpDiffusionTest, ZeroVolatilityAndZeroJumpsWithZeroDrift) {
    // No volatility, no jumps, no drift -> price stays constant
    JumpDiffusionFairPriceGenerator gen(
        make_config(Price{1'000'000}, 0.0, 0.0, Timestamp{1000}, 0.0, 0.0, 0.0), 42);

    gen.advance_to(Timestamp{1000});
    EXPECT_EQ(gen.true_price(), Price{1'000'000});

    gen.advance_to(Timestamp{5000});
    EXPECT_EQ(gen.true_price(), Price{1'000'000});
}

TEST_F(JumpDiffusionTest, HighJumpIntensityProducesLargerVariation) {
    // Compare variance between low and high jump intensity over many runs
    auto low_jump_config = make_config(Price{1'000'000}, 0.0, 0.01, Timestamp{1000}, 0.01, 0.0, 0.1);
    auto high_jump_config = make_config(Price{1'000'000}, 0.0, 0.01, Timestamp{1000}, 1.0, 0.0, 0.1);

    double low_variance = 0.0;
    double high_variance = 0.0;
    const int num_samples = 100;
    const double initial = 1'000'000.0;

    for (int i = 0; i < num_samples; ++i) {
        JumpDiffusionFairPriceGenerator low_gen(low_jump_config, static_cast<std::uint64_t>(i));
        JumpDiffusionFairPriceGenerator high_gen(high_jump_config, static_cast<std::uint64_t>(i + 1000));

        low_gen.advance_to(Timestamp{10000});
        high_gen.advance_to(Timestamp{10000});

        double low_ret = (static_cast<double>(low_gen.true_price().value()) - initial) / initial;
        double high_ret = (static_cast<double>(high_gen.true_price().value()) - initial) / initial;

        low_variance += low_ret * low_ret;
        high_variance += high_ret * high_ret;
    }

    // High jump intensity should produce higher variance
    EXPECT_GT(high_variance, low_variance);
}

TEST_F(JumpDiffusionTest, JumpsCanProduceLargePriceMovements) {
    // Configure large jumps to test adverse selection scenarios
    auto config = make_config(
        Price{1'000'000},
        0.0,             // no drift
        0.001,           // very low diffusion volatility
        Timestamp{1000},
        0.5,             // moderate jump intensity
        0.0,             // mean jump size
        0.2              // large jump std - allows for big jumps
    );

    bool found_large_move = false;
    const double threshold = 0.05; // 5% move

    // Run multiple simulations to find at least one large move
    for (std::uint64_t seed = 0; seed < 50 && !found_large_move; ++seed) {
        JumpDiffusionFairPriceGenerator gen(config, seed);
        double prev_price = static_cast<double>(gen.true_price().value());

        for (Timestamp t{1000}; t <= Timestamp{10000}; t += Timestamp{1000}) {
            gen.advance_to(t);
            double curr_price = static_cast<double>(gen.true_price().value());
            double pct_change = std::abs(curr_price - prev_price) / prev_price;

            if (pct_change > threshold) {
                found_large_move = true;
                break;
            }
            prev_price = curr_price;
        }
    }

    EXPECT_TRUE(found_large_move) << "Jump diffusion should be capable of producing large price movements";
}

TEST_F(JumpDiffusionTest, ImplementsIFairPriceSourceInterface) {
    auto config = make_config();
    JumpDiffusionFairPriceGenerator gen(config, 42);

    // Test that it can be used polymorphically
    IFairPriceSource* source = &gen;

    EXPECT_EQ(source->true_price(), Price{1'000'000});
    EXPECT_EQ(source->last_update(), Timestamp{0});

    source->advance_to(Timestamp{1000});
    EXPECT_EQ(source->last_update(), Timestamp{1000});
}
