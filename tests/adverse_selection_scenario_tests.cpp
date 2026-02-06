#include "simulation/fair_price.hpp"
#include "testing/test_harness.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// =============================================================================
// Custom Fair Price Source for Deterministic Testing
// =============================================================================

/**
 * Fair price source that follows a predetermined schedule.
 *
 * Takes a sorted list of (timestamp, price) pairs. On advance_to(t),
 * the current price is set to the latest entry with timestamp <= t.
 */
class ScheduledFairPriceSource : public IFairPriceSource {
public:
    explicit ScheduledFairPriceSource(std::vector<std::pair<Timestamp, Price>> schedule)
        : schedule_(std::move(schedule)) {
        if (!schedule_.empty()) {
            current_price_ = schedule_.front().second;
        }
    }

    void advance_to(Timestamp t) override {
        last_update_ = t;
        // Find the latest entry with timestamp <= t
        for (auto it = schedule_.rbegin(); it != schedule_.rend(); ++it) {
            if (it->first <= t) {
                current_price_ = it->second;
                return;
            }
        }
    }

    [[nodiscard]] Price true_price() const override { return current_price_; }
    [[nodiscard]] Timestamp last_update() const override { return last_update_; }

private:
    std::vector<std::pair<Timestamp, Price>> schedule_;
    Price current_price_{0};
    Timestamp last_update_{0};
};

// =============================================================================
// Test Fixture
// =============================================================================

class AdverseSelectionScenarioTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* env_dir = std::getenv("AS_TEST_OUTPUT_DIR");
        if (env_dir != nullptr && std::strlen(env_dir) > 0) {
            test_dir_ = fs::path(env_dir) / ("test_" + std::to_string(test_counter_++));
            preserve_output_ = true;
        } else {
            test_dir_ = fs::temp_directory_path() /
                        ("as_test_" + std::to_string(test_counter_++));
            preserve_output_ = false;
        }
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        if (!preserve_output_ && fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    /**
     * Set up harness with instrument, output dir, fair price source,
     * and agent metadata entries.
     */
    struct AgentEntry {
        ClientID id;
        std::string type;
    };

    testing::TestHarness
    setup_harness(std::vector<std::pair<Timestamp, Price>> fair_price_schedule,
                  std::vector<AgentEntry> agents) {

        testing::TestHarness harness;
        harness.add_instrument(InstrumentID{1});
        harness.set_output_directory(test_dir_);

        // Inject deterministic fair price source
        harness.engine().set_fair_price_source(
            std::make_unique<ScheduledFairPriceSource>(std::move(fair_price_schedule)));

        // Register agents in metadata
        auto* dc = harness.engine().data_collector();
        for (const auto& agent : agents) {
            dc->metadata().add_agent(agent.id, agent.type, nlohmann::json{}, 0);
        }

        return harness;
    }

    void verify_output_files() {
        EXPECT_TRUE(fs::exists(test_dir_ / "deltas.csv")) << "deltas.csv not created";
        EXPECT_TRUE(fs::exists(test_dir_ / "trades.csv")) << "trades.csv not created";
        EXPECT_TRUE(fs::exists(test_dir_ / "market_state.csv"))
            << "market_state.csv not created";
        EXPECT_TRUE(fs::exists(test_dir_ / "metadata.json"))
            << "metadata.json not created";
    }

    fs::path test_dir_;
    bool preserve_output_ = false;
    static inline int test_counter_ = 0;
};

// =============================================================================
// Scenario 0: MM buys, NoiseTrader is aggressor
// =============================================================================

TEST_F(AdverseSelectionScenarioTest, BasicMMBuyFill) {
    // Fair price constant at 950 (below fill price → adverse selection for MM buyer)
    auto harness =
        setup_harness({{Timestamp{0}, Price{950}}},
                      {{ClientID{10}, "MarketMaker"}, {ClientID{20}, "NoiseTrader"}});

    // t=100: MM posts resting BUY 100 @ 1000
    harness.schedule_order(Timestamp{100}, ClientID{10}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // t=200: NoiseTrader aggresses with SELL 100 @ 1000
    harness.schedule_order(Timestamp{200}, ClientID{20}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    harness.run(Timestamp{300});
    verify_output_files();

    // Expected in Python:
    // 1 fill, mm_side=BUY, quote_age=100, immediate_as=950-1000=-50
    // counterparty_type=NoiseTrader, counterparty_id=20
    const auto& pnl_mm = harness.engine().get_pnl(ClientID{10});
    EXPECT_EQ(pnl_mm.long_position.value(), 100u);
}

// =============================================================================
// Scenario 1: MM sells, InformedTrader is aggressor
// =============================================================================

TEST_F(AdverseSelectionScenarioTest, BasicMMSellFill) {
    // Fair price constant at 1050 (above fill price → adverse selection for MM seller)
    auto harness =
        setup_harness({{Timestamp{0}, Price{1050}}},
                      {{ClientID{10}, "MarketMaker"}, {ClientID{30}, "InformedTrader"}});

    // t=100: MM posts resting SELL 100 @ 1000
    harness.schedule_order(Timestamp{100}, ClientID{10}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    // t=200: InformedTrader aggresses with BUY 100 @ 1000
    harness.schedule_order(Timestamp{200}, ClientID{30}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    harness.run(Timestamp{300});
    verify_output_files();

    // Expected in Python:
    // 1 fill, mm_side=SELL, quote_age=100, immediate_as=1000-1050=-50
    // counterparty_type=InformedTrader, counterparty_id=30
    const auto& pnl_mm = harness.engine().get_pnl(ClientID{10});
    EXPECT_EQ(pnl_mm.short_position.value(), 100u);
}

// =============================================================================
// Scenario 2: MODIFY resets the quote age clock
// =============================================================================

TEST_F(AdverseSelectionScenarioTest, ModifyResetsQuoteAge) {
    // Fair price constant at 1000
    auto harness =
        setup_harness({{Timestamp{0}, Price{1000}}},
                      {{ClientID{10}, "MarketMaker"}, {ClientID{20}, "NoiseTrader"}});

    // t=100: MM posts resting BUY 100 @ 990
    harness.schedule_order(Timestamp{100}, ClientID{10}, InstrumentID{1}, Quantity{100},
                           Price{990}, OrderSide::BUY);

    // t=300: MM modifies order to 100 @ 995 (resets quote age clock)
    harness.schedule_modify(Timestamp{300}, ClientID{10}, OrderID{1}, Quantity{100},
                            Price{995});

    // t=500: NoiseTrader aggresses with SELL 100 @ 995
    harness.schedule_order(Timestamp{500}, ClientID{20}, InstrumentID{1}, Quantity{100},
                           Price{995}, OrderSide::SELL);

    harness.run(Timestamp{600});
    verify_output_files();

    // Expected in Python:
    // 1 fill, quote_age = 500 - 300 = 200 (MODIFY timestamp, not ADD)
    // immediate_as = 1000 - 995 = 5 (MM bought at 995, fair price 1000)
    const auto& pnl_mm = harness.engine().get_pnl(ClientID{10});
    EXPECT_EQ(pnl_mm.long_position.value(), 100u);
}

// =============================================================================
// Scenario 3: MM as aggressor — should NOT appear in analyzer output
// =============================================================================

TEST_F(AdverseSelectionScenarioTest, AggressorMMSkipped) {
    // Fair price constant at 1000
    auto harness =
        setup_harness({{Timestamp{0}, Price{1000}}},
                      {{ClientID{10}, "MarketMaker"}, {ClientID{20}, "NoiseTrader"}});

    // t=100: NoiseTrader posts resting SELL 100 @ 1000
    harness.schedule_order(Timestamp{100}, ClientID{20}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    // t=200: MM aggresses with BUY 100 @ 1000 (MM is the taker, not the maker)
    harness.schedule_order(Timestamp{200}, ClientID{10}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    harness.run(Timestamp{300});
    verify_output_files();

    // Expected in Python:
    // 0 MM maker fills (MM was the aggressor)
    const auto& pnl_mm = harness.engine().get_pnl(ClientID{10});
    EXPECT_EQ(pnl_mm.long_position.value(), 100u);
}

// =============================================================================
// Scenario 4: Realized adverse selection with changing fair price
// =============================================================================

TEST_F(AdverseSelectionScenarioTest, RealizedASWithChangingFairPrice) {
    // Fair price changes over time:
    // t=0..199: 950, t=200..299: 950, t=300..399: 920, t=400..499: 880, t=500+: 900
    auto harness = setup_harness({{Timestamp{0}, Price{950}},
                                  {Timestamp{300}, Price{920}},
                                  {Timestamp{400}, Price{880}},
                                  {Timestamp{500}, Price{900}}},
                                 {{ClientID{10}, "MarketMaker"},
                                  {ClientID{20}, "NoiseTrader"},
                                  {ClientID{99}, "NoiseTrader"}});

    // t=100: MM posts resting BUY 100 @ 1000
    harness.schedule_order(Timestamp{100}, ClientID{10}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::BUY);

    // t=200: NoiseTrader aggresses with SELL 100 @ 1000 (fill at fair_price=950)
    harness.schedule_order(Timestamp{200}, ClientID{20}, InstrumentID{1}, Quantity{100},
                           Price{1000}, OrderSide::SELL);

    // Dummy orders to generate market_state.csv entries at future timestamps
    // so realized AS horizon lookups can find fair prices at t=300, 400, 500
    harness.schedule_order(Timestamp{300}, ClientID{99}, InstrumentID{1}, Quantity{10},
                           Price{500}, OrderSide::BUY);
    harness.schedule_order(Timestamp{400}, ClientID{99}, InstrumentID{1}, Quantity{10},
                           Price{501}, OrderSide::BUY);
    harness.schedule_order(Timestamp{500}, ClientID{99}, InstrumentID{1}, Quantity{10},
                           Price{502}, OrderSide::BUY);

    harness.run(Timestamp{600});
    verify_output_files();

    // Expected in Python (horizons=[100, 200, 300]):
    // 1 MM fill at t=200, mm_side=BUY, fill_price=1000, fair_price=950
    // immediate_as = 950 - 1000 = -50
    // realized_as@100 = fair_price_at(t>=300) - 1000 = 920 - 1000 = -80
    // realized_as@200 = fair_price_at(t>=400) - 1000 = 880 - 1000 = -120
    // realized_as@300 = fair_price_at(t>=500) - 1000 = 900 - 1000 = -100
}
