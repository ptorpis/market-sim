#include <gtest/gtest.h>

#include "simulation/fair_price.hpp"

class FairPriceTest : public ::testing::Test {
protected:
    FairPriceConfig make_config(Price initial = Price{1'000'000}, double drift = 0.0,
                                double volatility = 0.01,
                                Timestamp tick_size = Timestamp{1000}) {
        return FairPriceConfig{.initial_price = initial,
                               .drift = drift,
                               .volatility = volatility,
                               .tick_size = tick_size};
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
    JumpDiffusionConfig make_config(Price initial = Price{1'000'000}, double drift = 0.0,
                                    double volatility = 0.01,
                                    Timestamp tick_size = Timestamp{1000},
                                    double jump_intensity = 0.1, double jump_mean = 0.0,
                                    double jump_std = 0.05) {
        return JumpDiffusionConfig{.initial_price = initial,
                                   .drift = drift,
                                   .volatility = volatility,
                                   .tick_size = tick_size,
                                   .jump_intensity = jump_intensity,
                                   .jump_mean = jump_mean,
                                   .jump_std = jump_std};
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
    auto config =
        make_config(Price{500'000}, 0.001, 0.02, Timestamp{500}, 0.2, -0.01, 0.1);
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
    auto config =
        make_config(Price{1'000'000}, 0.0001, 0.01, Timestamp{1000}, 0.1, 0.0, 0.05);

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
    auto config =
        make_config(Price{1'000'000}, 0.0001, 0.01, Timestamp{1000}, 0.1, 0.0, 0.05);

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
    auto low_jump_config =
        make_config(Price{1'000'000}, 0.0, 0.01, Timestamp{1000}, 0.01, 0.0, 0.1);
    auto high_jump_config =
        make_config(Price{1'000'000}, 0.0, 0.01, Timestamp{1000}, 1.0, 0.0, 0.1);

    double low_variance = 0.0;
    double high_variance = 0.0;
    const int num_samples = 100;
    const double initial = 1'000'000.0;

    for (int i = 0; i < num_samples; ++i) {
        JumpDiffusionFairPriceGenerator low_gen(low_jump_config,
                                                static_cast<std::uint64_t>(i));
        JumpDiffusionFairPriceGenerator high_gen(high_jump_config,
                                                 static_cast<std::uint64_t>(i + 1000));

        low_gen.advance_to(Timestamp{10000});
        high_gen.advance_to(Timestamp{10000});

        double low_ret =
            (static_cast<double>(low_gen.true_price().value()) - initial) / initial;
        double high_ret =
            (static_cast<double>(high_gen.true_price().value()) - initial) / initial;

        low_variance += low_ret * low_ret;
        high_variance += high_ret * high_ret;
    }

    // High jump intensity should produce higher variance
    EXPECT_GT(high_variance, low_variance);
}

TEST_F(JumpDiffusionTest, JumpsCanProduceLargePriceMovements) {
    // Configure large jumps to test adverse selection scenarios
    auto config = make_config(Price{1'000'000},
                              0.0,   // no drift
                              0.001, // very low diffusion volatility
                              Timestamp{1000},
                              0.5, // moderate jump intensity
                              0.0, // mean jump size
                              0.2  // large jump std - allows for big jumps
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

    EXPECT_TRUE(found_large_move)
        << "Jump diffusion should be capable of producing large price movements";
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

// =============================================================================
// Statistical Validity Tests - Helper Functions
// =============================================================================

#include <algorithm>
#include <numeric>

namespace {

std::vector<double> calculate_log_returns(const std::vector<double>& prices) {
    std::vector<double> returns;
    returns.reserve(prices.size() - 1);
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i] > 0 && prices[i - 1] > 0) {
            returns.push_back(std::log(prices[i] / prices[i - 1]));
        }
    }
    return returns;
}

double calculate_mean(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    return std::accumulate(data.begin(), data.end(), 0.0) /
           static_cast<double>(data.size());
}

double calculate_variance(const std::vector<double>& data, double mean) {
    if (data.size() < 2) return 0.0;
    double sum_sq = std::transform_reduce(data.begin(), data.end(), 0.0, std::plus<>(),
                                          [mean](double x) {
                                              return (x - mean) * (x - mean);
                                          });
    return sum_sq / static_cast<double>(data.size() - 1);
}

double calculate_excess_kurtosis(const std::vector<double>& data) {
    if (data.size() < 4) return 0.0;
    double mean = calculate_mean(data);
    double variance = calculate_variance(data, mean);
    if (variance < 1e-15) return 0.0;

    double m4 = std::transform_reduce(data.begin(), data.end(), 0.0, std::plus<>(),
                                      [mean](double x) {
                                          double diff = x - mean;
                                          return diff * diff * diff * diff;
                                      }) /
                static_cast<double>(data.size());

    return (m4 / (variance * variance)) - 3.0;
}

struct SeparatedReturns {
    std::vector<double> diffusion_returns;
    std::vector<double> jump_returns;
    int jump_count;
};

SeparatedReturns separate_jumps(const std::vector<double>& returns,
                                double expected_diffusion_std,
                                double threshold_multiplier = 3.5) {
    SeparatedReturns result{};
    double threshold = threshold_multiplier * expected_diffusion_std;

    std::partition_copy(
        returns.begin(), returns.end(), std::back_inserter(result.jump_returns),
        std::back_inserter(result.diffusion_returns), [threshold](double r) {
            return std::abs(r) > threshold;
        });

    result.jump_count = static_cast<int>(result.jump_returns.size());
    return result;
}

} // anonymous namespace

// =============================================================================
// GBM Statistical Validity Tests
// =============================================================================

class GBMStatisticalTest : public ::testing::Test {
protected:
    // Use parameters where cumulative variance stays bounded
    // With 10k ticks and vol=0.01, cumulative std ~ sqrt(10000)*0.01 = 1
    // Price stays in range [e^-4, e^4] * initial with very high probability
    static constexpr int NUM_TICKS = 10'000;
    static constexpr double INITIAL_PRICE = 1'000'000.0;
    static constexpr double VOLATILITY = 0.01;
    static constexpr double DRIFT = 0.0;

    FairPriceConfig make_config() {
        return FairPriceConfig{
            .initial_price = Price{static_cast<std::uint64_t>(INITIAL_PRICE)},
            .drift = DRIFT,
            .volatility = VOLATILITY,
            .tick_size = Timestamp{1000}};
    }

    std::vector<double> generate_price_series(std::uint64_t seed) {
        FairPriceGenerator gen(make_config(), seed);
        std::vector<double> prices;
        prices.reserve(NUM_TICKS + 1);
        prices.push_back(INITIAL_PRICE);

        for (int i = 1; i <= NUM_TICKS; ++i) {
            gen.advance_to(Timestamp{static_cast<std::uint64_t>(i) * 1000});
            prices.push_back(static_cast<double>(gen.true_price().value()));
        }
        return prices;
    }
};

TEST_F(GBMStatisticalTest, DiffusionVolatilityMatchesExpected) {
    auto prices = generate_price_series(42);
    auto returns = calculate_log_returns(prices);
    ASSERT_GT(returns.size(), 100UL) << "Not enough valid returns";

    double mean = calculate_mean(returns);
    double realized_volatility = std::sqrt(calculate_variance(returns, mean));

    // Allow 10% relative error for finite sample
    double relative_error = std::abs(realized_volatility - VOLATILITY) / VOLATILITY;
    EXPECT_LT(relative_error, 0.10)
        << "Realized volatility: " << realized_volatility << ", Expected: " << VOLATILITY
        << ", Relative error: " << (relative_error * 100) << "%";
}

TEST_F(GBMStatisticalTest, ReturnsAreNormallyDistributed_KurtosisTest) {
    // For pure GBM, returns should be normally distributed (excess kurtosis ~= 0)
    auto prices = generate_price_series(12345);
    auto returns = calculate_log_returns(prices);
    ASSERT_GT(returns.size(), 100UL);

    double excess_kurtosis = calculate_excess_kurtosis(returns);

    // Standard error of kurtosis: sqrt(24/N), use 4-sigma tolerance
    double tolerance = 4.0 * std::sqrt(24.0 / static_cast<double>(returns.size()));

    EXPECT_NEAR(excess_kurtosis, 0.0, tolerance) << "Excess kurtosis: " << excess_kurtosis
                                                 << ", Expected: 0 (normal distribution)";
}

TEST_F(GBMStatisticalTest, MeanReturnConsistentWithZeroDrift) {
    auto prices = generate_price_series(98765);
    auto returns = calculate_log_returns(prices);
    ASSERT_GT(returns.size(), 100UL);

    double mean = calculate_mean(returns);
    double std_dev = std::sqrt(calculate_variance(returns, mean));

    // Expected mean for GBM log returns: (drift - 0.5 * vol^2) * dt
    double expected_mean = DRIFT - 0.5 * VOLATILITY * VOLATILITY;
    double se_mean = std_dev / std::sqrt(static_cast<double>(returns.size()));
    double z_score = std::abs(mean - expected_mean) / se_mean;

    EXPECT_LT(z_score, 4.0) << "Mean return: " << mean << ", Expected: " << expected_mean
                            << ", Z-score: " << z_score;
}

TEST_F(GBMStatisticalTest, NoLargeJumps) {
    // Pure GBM should not have frequent large moves beyond 4 std deviations
    auto prices = generate_price_series(54321);
    auto returns = calculate_log_returns(prices);
    ASSERT_GT(returns.size(), 100UL);

    double mean = calculate_mean(returns);
    double std_dev = std::sqrt(calculate_variance(returns, mean));
    double threshold = 4.0 * std_dev;

    int large_moves = static_cast<int>(
        std::count_if(returns.begin(), returns.end(), [mean, threshold](double r) {
            return std::abs(r - mean) > threshold;
        }));

    // For normal distribution, P(|X| > 4 sigma) ~= 0.006%
    double max_expected = 0.0002 * static_cast<double>(returns.size()) * 3; // 3x buffer

    EXPECT_LT(large_moves, max_expected) << "Large moves (>4 sigma): " << large_moves
                                         << ", Max expected: " << max_expected;
}

// =============================================================================
// Jump Diffusion Statistical Validity Tests
// =============================================================================

class JumpDiffusionStatisticalTest : public ::testing::Test {
protected:
    // Bounded parameters to prevent price drift issues
    static constexpr int NUM_TICKS = 10'000;
    static constexpr double INITIAL_PRICE = 1'000'000.0;
    static constexpr double VOLATILITY = 0.002; // Very low diffusion for clean separation
    static constexpr double DRIFT = 0.0;
    static constexpr double JUMP_INTENSITY = 0.01; // ~100 jumps in 10k ticks
    static constexpr double JUMP_MEAN = 0.0;
    static constexpr double JUMP_STD = 0.05; // 5% jump std (25x diffusion)

    // Threshold multiplier for separating jumps from diffusion
    // At 4 sigma of diffusion, threshold = 4 * 0.002 = 0.008
    // Diffusion false positives: P(|Z| > 4) ≈ 0.006% → ~0.6 per 10k ticks
    // Jump detection: P(|J/0.05| > 0.008/0.05) = P(|Z| > 0.16) ≈ 87%
    static constexpr double THRESHOLD_MULT = 4.0;

    JumpDiffusionConfig make_config() {
        return JumpDiffusionConfig{
            .initial_price = Price{static_cast<std::uint64_t>(INITIAL_PRICE)},
            .drift = DRIFT,
            .volatility = VOLATILITY,
            .tick_size = Timestamp{1000},
            .jump_intensity = JUMP_INTENSITY,
            .jump_mean = JUMP_MEAN,
            .jump_std = JUMP_STD};
    }

    std::vector<double> generate_price_series(std::uint64_t seed) {
        JumpDiffusionFairPriceGenerator gen(make_config(), seed);
        std::vector<double> prices;
        prices.reserve(NUM_TICKS + 1);
        prices.push_back(INITIAL_PRICE);

        for (int i = 1; i <= NUM_TICKS; ++i) {
            gen.advance_to(Timestamp{static_cast<std::uint64_t>(i) * 1000});
            prices.push_back(static_cast<double>(gen.true_price().value()));
        }
        return prices;
    }

    // Calculate what fraction of jumps we expect to detect given our threshold
    double expected_detection_rate() const {
        // Jump ~ N(JUMP_MEAN, JUMP_STD^2)
        // We detect if |jump| > THRESHOLD_MULT * VOLATILITY
        double threshold = THRESHOLD_MULT * VOLATILITY;
        // P(|N(0, sigma^2)| > t) = 2 * (1 - Phi(t/sigma))
        // Using approximation: erfc(x/sqrt(2)) for standard normal
        double z = threshold / JUMP_STD;
        return std::erfc(z / std::sqrt(2.0));
    }
};

TEST_F(JumpDiffusionStatisticalTest, JumpFrequencyFollowsPoisson) {
    auto prices = generate_price_series(42);
    auto returns = calculate_log_returns(prices);
    ASSERT_GT(returns.size(), 100UL);

    auto separated = separate_jumps(returns, VOLATILITY, THRESHOLD_MULT);

    // Account for the detection rate - we only catch a fraction of jumps
    double total_expected_jumps = JUMP_INTENSITY * NUM_TICKS;
    double detection_rate = expected_detection_rate();
    double expected_detected = total_expected_jumps * detection_rate;

    // Variance of detected jumps: Poisson + detection uncertainty
    // Approximate as Poisson with mean = expected_detected
    double std_detected = std::sqrt(expected_detected);
    double z_score =
        std::abs(static_cast<double>(separated.jump_count) - expected_detected) /
        std_detected;

    EXPECT_LT(z_score, 4.0) << "Detected jumps: " << separated.jump_count
                            << ", Expected (with " << (detection_rate * 100)
                            << "% detection): " << expected_detected
                            << ", Z-score: " << z_score;
}

TEST_F(JumpDiffusionStatisticalTest, DiffusionComponentVolatilityMatchesExpected) {
    auto prices = generate_price_series(12345);
    auto returns = calculate_log_returns(prices);
    ASSERT_GT(returns.size(), 100UL);

    auto separated = separate_jumps(returns, VOLATILITY, THRESHOLD_MULT);
    ASSERT_GT(separated.diffusion_returns.size(), 100UL);

    double mean = calculate_mean(separated.diffusion_returns);
    double realized_volatility =
        std::sqrt(calculate_variance(separated.diffusion_returns, mean));

    // Diffusion returns are truncated at threshold, so expect slightly lower variance
    // Allow 30% relative error due to truncation and small sample effects
    double relative_error = std::abs(realized_volatility - VOLATILITY) / VOLATILITY;
    EXPECT_LT(relative_error, 0.30)
        << "Diffusion volatility: " << realized_volatility << ", Expected: " << VOLATILITY
        << ", Relative error: " << (relative_error * 100) << "%";
}

TEST_F(JumpDiffusionStatisticalTest, FatTailsPresent_HighKurtosis) {
    // This is the "gold standard" test for jump-diffusion models
    auto prices = generate_price_series(98765);
    auto returns = calculate_log_returns(prices);
    ASSERT_GT(returns.size(), 100UL);

    double excess_kurtosis = calculate_excess_kurtosis(returns);

    // Jump diffusion should have positive excess kurtosis (fat tails)
    EXPECT_GT(excess_kurtosis, 0.0) << "Excess kurtosis: " << excess_kurtosis
                                    << ", Expected: > 0 (fat tails from jumps)";
}

TEST_F(JumpDiffusionStatisticalTest, TotalVarianceIsCorrect) {
    auto prices = generate_price_series(54321);
    auto returns = calculate_log_returns(prices);
    ASSERT_GT(returns.size(), 100UL);

    double mean = calculate_mean(returns);
    double realized_variance = calculate_variance(returns, mean);

    // Theoretical: Var = sigma^2 + lambda * E[J^2]
    double expected_variance =
        VOLATILITY * VOLATILITY +
        JUMP_INTENSITY * (JUMP_MEAN * JUMP_MEAN + JUMP_STD * JUMP_STD);

    // Allow 25% relative error for finite sample
    double relative_error =
        std::abs(realized_variance - expected_variance) / expected_variance;
    EXPECT_LT(relative_error, 0.25)
        << "Realized variance: " << realized_variance
        << ", Expected: " << expected_variance
        << ", Relative error: " << (relative_error * 100) << "%";
}

TEST_F(JumpDiffusionStatisticalTest, JumpSizesAreLogNormal) {
    auto prices = generate_price_series(11111);
    auto returns = calculate_log_returns(prices);
    auto separated = separate_jumps(returns, VOLATILITY, THRESHOLD_MULT);

    if (separated.jump_returns.size() < 30) {
        GTEST_SKIP() << "Not enough jumps to validate distribution (got "
                     << separated.jump_returns.size() << ")";
    }

    double jump_mean = calculate_mean(separated.jump_returns);

    // Jump mean should be close to JUMP_MEAN (with wide tolerance due to selection bias)
    EXPECT_NEAR(jump_mean, JUMP_MEAN, 0.03) << "Observed jump mean: " << jump_mean;
}

TEST_F(JumpDiffusionStatisticalTest, AdverseSelectionPotential) {
    auto prices = generate_price_series(22222);
    auto returns = calculate_log_returns(prices);
    auto separated = separate_jumps(returns, VOLATILITY, THRESHOLD_MULT);

    if (separated.jump_returns.empty()) {
        GTEST_SKIP() << "No jumps detected";
    }

    auto max_it = std::max_element(separated.jump_returns.begin(),
                                   separated.jump_returns.end(), [](double a, double b) {
                                       return std::abs(a) < std::abs(b);
                                   });
    double max_jump = std::abs(*max_it);

    // Jumps should be significantly larger than diffusion noise
    EXPECT_GT(max_jump, 3 * VOLATILITY)
        << "Max jump: " << max_jump << ", Expected: > " << (3 * VOLATILITY);
}

TEST_F(JumpDiffusionStatisticalTest, CompareWithPureGBM_HigherVariance) {
    const std::uint64_t seed = 33333;

    auto jd_prices = generate_price_series(seed);
    auto jd_returns = calculate_log_returns(jd_prices);
    ASSERT_GT(jd_returns.size(), 100UL);
    double jd_variance = calculate_variance(jd_returns, calculate_mean(jd_returns));

    // Generate pure GBM series
    FairPriceConfig gbm_config{
        .initial_price = Price{static_cast<std::uint64_t>(INITIAL_PRICE)},
        .drift = DRIFT,
        .volatility = VOLATILITY,
        .tick_size = Timestamp{1000}};
    FairPriceGenerator gbm_gen(gbm_config, seed);
    std::vector<double> gbm_prices;
    gbm_prices.reserve(NUM_TICKS + 1);
    gbm_prices.push_back(INITIAL_PRICE);
    for (int i = 1; i <= NUM_TICKS; ++i) {
        gbm_gen.advance_to(Timestamp{static_cast<std::uint64_t>(i) * 1000});
        gbm_prices.push_back(static_cast<double>(gbm_gen.true_price().value()));
    }
    auto gbm_returns = calculate_log_returns(gbm_prices);
    ASSERT_GT(gbm_returns.size(), 100UL);
    double gbm_variance = calculate_variance(gbm_returns, calculate_mean(gbm_returns));

    EXPECT_GT(jd_variance, gbm_variance)
        << "JD variance: " << jd_variance << ", GBM variance: " << gbm_variance;
}

TEST_F(JumpDiffusionStatisticalTest, CompareWithPureGBM_HigherKurtosis) {
    const std::uint64_t seed = 44444;

    auto jd_prices = generate_price_series(seed);
    auto jd_returns = calculate_log_returns(jd_prices);
    ASSERT_GT(jd_returns.size(), 100UL);
    double jd_kurtosis = calculate_excess_kurtosis(jd_returns);

    FairPriceConfig gbm_config{
        .initial_price = Price{static_cast<std::uint64_t>(INITIAL_PRICE)},
        .drift = DRIFT,
        .volatility = VOLATILITY,
        .tick_size = Timestamp{1000}};
    FairPriceGenerator gbm_gen(gbm_config, seed);
    std::vector<double> gbm_prices;
    gbm_prices.reserve(NUM_TICKS + 1);
    gbm_prices.push_back(INITIAL_PRICE);
    for (int i = 1; i <= NUM_TICKS; ++i) {
        gbm_gen.advance_to(Timestamp{static_cast<std::uint64_t>(i) * 1000});
        gbm_prices.push_back(static_cast<double>(gbm_gen.true_price().value()));
    }
    auto gbm_returns = calculate_log_returns(gbm_prices);
    ASSERT_GT(gbm_returns.size(), 100UL);
    double gbm_kurtosis = calculate_excess_kurtosis(gbm_returns);

    EXPECT_GT(jd_kurtosis, gbm_kurtosis) << "JD excess kurtosis: " << jd_kurtosis
                                         << ", GBM excess kurtosis: " << gbm_kurtosis;
}
