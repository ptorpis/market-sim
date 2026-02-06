#include <gtest/gtest.h>

#include "config/config_loader.hpp"

#include <nlohmann/json.hpp>
#include <variant>

using json = nlohmann::json;

// =============================================================================
// FairPriceConfig
// =============================================================================

TEST(ConfigLoaderTest, ParseFairPriceConfig) {
    json j = {
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    FairPriceConfig config = j.get<FairPriceConfig>();

    EXPECT_EQ(config.initial_price, Price{1000000});
    EXPECT_DOUBLE_EQ(config.drift, 0.0001);
    EXPECT_DOUBLE_EQ(config.volatility, 0.005);
    EXPECT_EQ(config.tick_size, Timestamp{1000});
}

TEST(ConfigLoaderTest, FairPriceConfigMissingFieldThrows) {
    json j = {
        {"initial_price", 1000000},
        {"drift", 0.0001}
        // Missing volatility and tick_size
    };

    EXPECT_THROW(j.get<FairPriceConfig>(), json::out_of_range);
}

// =============================================================================
// JumpDiffusionConfig
// =============================================================================

TEST(ConfigLoaderTest, ParseJumpDiffusionConfig) {
    json j = {
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000},
        {"jump_intensity", 0.1},
        {"jump_mean", 0.0},
        {"jump_std", 0.05}
    };

    JumpDiffusionConfig config = j.get<JumpDiffusionConfig>();

    EXPECT_EQ(config.initial_price, Price{1000000});
    EXPECT_DOUBLE_EQ(config.drift, 0.0001);
    EXPECT_DOUBLE_EQ(config.volatility, 0.005);
    EXPECT_EQ(config.tick_size, Timestamp{1000});
    EXPECT_DOUBLE_EQ(config.jump_intensity, 0.1);
    EXPECT_DOUBLE_EQ(config.jump_mean, 0.0);
    EXPECT_DOUBLE_EQ(config.jump_std, 0.05);
}

TEST(ConfigLoaderTest, ParseFairPriceModelConfigGBM) {
    json j = {
        {"model", "gbm"},
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    FairPriceModelConfig config = parse_fair_price_config(j);

    ASSERT_TRUE(std::holds_alternative<FairPriceConfig>(config));
    auto& gbm = std::get<FairPriceConfig>(config);
    EXPECT_EQ(gbm.initial_price, Price{1000000});
    EXPECT_DOUBLE_EQ(gbm.volatility, 0.005);
}

TEST(ConfigLoaderTest, ParseFairPriceModelConfigJumpDiffusion) {
    json j = {
        {"model", "jump_diffusion"},
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000},
        {"jump_intensity", 0.2},
        {"jump_mean", -0.01},
        {"jump_std", 0.1}
    };

    FairPriceModelConfig config = parse_fair_price_config(j);

    ASSERT_TRUE(std::holds_alternative<JumpDiffusionConfig>(config));
    auto& jd = std::get<JumpDiffusionConfig>(config);
    EXPECT_EQ(jd.initial_price, Price{1000000});
    EXPECT_DOUBLE_EQ(jd.jump_intensity, 0.2);
    EXPECT_DOUBLE_EQ(jd.jump_mean, -0.01);
    EXPECT_DOUBLE_EQ(jd.jump_std, 0.1);
}

TEST(ConfigLoaderTest, ParseFairPriceModelConfigDefaultsToGBM) {
    // Without "model" field, should default to GBM
    json j = {
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    FairPriceModelConfig config = parse_fair_price_config(j);

    ASSERT_TRUE(std::holds_alternative<FairPriceConfig>(config));
}

// =============================================================================
// NoiseTraderConfig
// =============================================================================

TEST(ConfigLoaderTest, ParseNoiseTraderConfig) {
    json j = {
        {"instrument", 1},
        {"observation_noise", 50.0},
        {"spread", 36},
        {"min_quantity", 10},
        {"max_quantity", 100},
        {"min_interval", 50},
        {"max_interval", 200},
        {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
    };

    NoiseTraderConfig config = j.get<NoiseTraderConfig>();

    EXPECT_EQ(config.instrument, InstrumentID{1});
    EXPECT_DOUBLE_EQ(config.observation_noise, 50.0);
    EXPECT_EQ(config.spread, Price{36});
    EXPECT_EQ(config.min_quantity, Quantity{10});
    EXPECT_EQ(config.max_quantity, Quantity{100});
    EXPECT_EQ(config.min_interval, Timestamp{50});
    EXPECT_EQ(config.max_interval, Timestamp{200});
    EXPECT_EQ(config.adverse_fill_threshold, Price{100});
    EXPECT_EQ(config.stale_order_threshold, Price{1000});
}

// =============================================================================
// NoiseTraderGroupConfig
// =============================================================================

TEST(ConfigLoaderTest, ParseNoiseTraderGroupConfig) {
    json j = {
        {"count", 5},
        {"start_client_id", 100},
        {"base_seed", 42},
        {"initial_wakeup_start", 10},
        {"initial_wakeup_step", 20},
        {"config", {
            {"instrument", 1},
            {"observation_noise", 50.0},
            {"spread", 36},
            {"min_quantity", 10},
            {"max_quantity", 100},
            {"min_interval", 50},
            {"max_interval", 200},
            {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
        }}
    };

    NoiseTraderGroupConfig config = j.get<NoiseTraderGroupConfig>();

    EXPECT_EQ(config.count, 5ULL);
    EXPECT_EQ(config.start_client_id, ClientID{100});
    EXPECT_EQ(config.base_seed, 42ULL);
    EXPECT_EQ(config.initial_wakeup_start, Timestamp{10});
    EXPECT_EQ(config.initial_wakeup_step, Timestamp{20});
    EXPECT_EQ(config.config.instrument, InstrumentID{1});
    EXPECT_DOUBLE_EQ(config.config.observation_noise, 50.0);
    EXPECT_EQ(config.config.spread, Price{36});
}

// =============================================================================
// MarketMakerConfig
// =============================================================================

TEST(ConfigLoaderTest, ParseMarketMakerConfig) {
    json j = {
        {"instrument", 1},
        {"observation_noise", 10.0},
        {"half_spread", 5},
        {"quote_size", 50},
        {"update_interval", 100},
        {"inventory_skew_factor", 0.5},
        {"max_position", 500}
    };

    MarketMakerConfig config = j.get<MarketMakerConfig>();

    EXPECT_EQ(config.instrument, InstrumentID{1});
    EXPECT_DOUBLE_EQ(config.observation_noise, 10.0);
    EXPECT_EQ(config.half_spread, Price{5});
    EXPECT_EQ(config.quote_size, Quantity{50});
    EXPECT_EQ(config.update_interval, Timestamp{100});
    EXPECT_DOUBLE_EQ(config.inventory_skew_factor, 0.5);
    EXPECT_EQ(config.max_position, Quantity{500});
}

// =============================================================================
// InformedTraderConfig
// =============================================================================

TEST(ConfigLoaderTest, ParseInformedTraderConfig) {
    json j = {
        {"instrument", 1},
        {"min_quantity", 20},
        {"max_quantity", 80},
        {"min_interval", 100},
        {"max_interval", 500},
        {"min_edge", 3},
        {"observation_noise", 5.0},
        {"adverse_fill_threshold", 50},
            {"stale_order_threshold", 500}
    };

    InformedTraderConfig config = j.get<InformedTraderConfig>();

    EXPECT_EQ(config.instrument, InstrumentID{1});
    EXPECT_EQ(config.min_quantity, Quantity{20});
    EXPECT_EQ(config.max_quantity, Quantity{80});
    EXPECT_EQ(config.min_interval, Timestamp{100});
    EXPECT_EQ(config.max_interval, Timestamp{500});
    EXPECT_EQ(config.min_edge, Price{3});
    EXPECT_DOUBLE_EQ(config.observation_noise, 5.0);
    EXPECT_EQ(config.adverse_fill_threshold, Price{50});
    EXPECT_EQ(config.stale_order_threshold, Price{500});
}

// =============================================================================
// InitialOrder
// =============================================================================

TEST(ConfigLoaderTest, ParseInitialOrderBuy) {
    json j = {
        {"instrument", 1},
        {"side", "BUY"},
        {"price", 999900},
        {"quantity", 500}
    };

    InitialOrder order = j.get<InitialOrder>();

    EXPECT_EQ(order.instrument, InstrumentID{1});
    EXPECT_EQ(order.side, OrderSide::BUY);
    EXPECT_EQ(order.price, Price{999900});
    EXPECT_EQ(order.quantity, Quantity{500});
}

TEST(ConfigLoaderTest, ParseInitialOrderSell) {
    json j = {
        {"instrument", 1},
        {"side", "SELL"},
        {"price", 1000100},
        {"quantity", 500}
    };

    InitialOrder order = j.get<InitialOrder>();

    EXPECT_EQ(order.side, OrderSide::SELL);
}

// =============================================================================
// AgentConfig
// =============================================================================

TEST(ConfigLoaderTest, ParseAgentConfigNoiseTrader) {
    json j = {
        {"client_id", 1},
        {"type", "NoiseTrader"},
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {
            {"instrument", 1},
            {"observation_noise", 50.0},
            {"spread", 36},
            {"min_quantity", 10},
            {"max_quantity", 100},
            {"min_interval", 50},
            {"max_interval", 200},
            {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
        }}
    };

    AgentConfig config = j.get<AgentConfig>();

    EXPECT_EQ(config.id, ClientID{1});
    EXPECT_EQ(config.type, "NoiseTrader");
    EXPECT_EQ(config.initial_wakeup, Timestamp{10});
    EXPECT_EQ(config.seed, 100ULL);
    EXPECT_EQ(config.noise_trader.instrument, InstrumentID{1});
}

TEST(ConfigLoaderTest, ParseAgentConfigMarketMaker) {
    json j = {
        {"client_id", 10},
        {"type", "MarketMaker"},
        {"initial_wakeup", 5},
        {"seed", 999},
        {"config", {
            {"instrument", 1},
            {"observation_noise", 10.0},
            {"half_spread", 5},
            {"quote_size", 50},
            {"update_interval", 100},
            {"inventory_skew_factor", 0.5},
            {"max_position", 500}
        }}
    };

    AgentConfig config = j.get<AgentConfig>();

    EXPECT_EQ(config.id, ClientID{10});
    EXPECT_EQ(config.type, "MarketMaker");
    EXPECT_EQ(config.market_maker.half_spread, Price{5});
}

TEST(ConfigLoaderTest, ParseAgentConfigInformedTrader) {
    json j = {
        {"client_id", 20},
        {"type", "InformedTrader"},
        {"initial_wakeup", 50},
        {"seed", 777},
        {"config", {
            {"instrument", 1},
            {"min_quantity", 20},
            {"max_quantity", 80},
            {"min_interval", 100},
            {"max_interval", 500},
            {"min_edge", 3},
            {"observation_noise", 5.0},
            {"adverse_fill_threshold", 50},
            {"stale_order_threshold", 500}
        }}
    };

    AgentConfig config = j.get<AgentConfig>();

    EXPECT_EQ(config.id, ClientID{20});
    EXPECT_EQ(config.type, "InformedTrader");
    EXPECT_EQ(config.informed_trader.min_edge, Price{3});
}

TEST(ConfigLoaderTest, UnknownAgentTypeThrows) {
    json j = {
        {"client_id", 1},
        {"type", "UnknownAgent"},
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    EXPECT_THROW(j.get<AgentConfig>(), std::runtime_error);
}

// =============================================================================
// SimulationConfig
// =============================================================================

TEST(ConfigLoaderTest, ParseFullSimulationConfig) {
    json j = {
        {"simulation", {
            {"latency", 10},
            {"duration", 1000},
            {"output_dir", "./output"},
            {"pnl_snapshot_interval", 100}
        }},
        {"instruments", {1, 2}},
        {"fair_price", {
            {"initial_price", 1000000},
            {"drift", 0.0001},
            {"volatility", 0.005},
            {"tick_size", 1000},
            {"seed", 43}
        }},
        {"agents", json::array()},
        {"initial_orders", json::array()}
    };

    SimulationConfig config = j.get<SimulationConfig>();

    EXPECT_EQ(config.latency, Timestamp{10});
    EXPECT_EQ(config.duration, Timestamp{1000});
    EXPECT_EQ(config.output_dir, "./output");
    EXPECT_EQ(config.pnl_snapshot_interval, Timestamp{100});
    EXPECT_EQ(config.instruments.size(), 2ULL);
    EXPECT_EQ(config.instruments[0], InstrumentID{1});
    EXPECT_EQ(config.instruments[1], InstrumentID{2});
    ASSERT_TRUE(std::holds_alternative<FairPriceConfig>(config.fair_price));
    EXPECT_EQ(std::get<FairPriceConfig>(config.fair_price).initial_price, Price{1000000});
    EXPECT_EQ(config.fair_price_seed, 43ULL);
}

TEST(ConfigLoaderTest, SimulationConfigWithMinimalFields) {
    json j = json::object();  // Empty object, not null

    SimulationConfig config = j.get<SimulationConfig>();

    // Should have struct defaults
    EXPECT_EQ(config.latency, Timestamp{0});
    EXPECT_EQ(config.duration, Timestamp{1000});
    EXPECT_EQ(config.output_dir, "./output");
    EXPECT_EQ(config.pnl_snapshot_interval, Timestamp{100});
    EXPECT_TRUE(config.instruments.empty());
    EXPECT_TRUE(config.agents.empty());
    EXPECT_TRUE(config.initial_orders.empty());
}

TEST(ConfigLoaderTest, SimulationConfigWithNoiseTraderGroup) {
    json j = {
        {"noise_traders", {
            {"count", 10},
            {"start_client_id", 1},
            {"base_seed", 100},
            {"initial_wakeup_start", 5},
            {"initial_wakeup_step", 10},
            {"config", {
                {"instrument", 1},
                {"observation_noise", 50.0},
                {"spread", 36},
                {"min_quantity", 10},
                {"max_quantity", 100},
                {"min_interval", 50},
                {"max_interval", 200},
                {"adverse_fill_threshold", 100},
                {"stale_order_threshold", 1000}
            }}
        }}
    };

    SimulationConfig config = j.get<SimulationConfig>();

    ASSERT_TRUE(config.noise_traders.has_value());
    EXPECT_EQ(config.noise_traders->count, 10ULL);
    EXPECT_EQ(config.noise_traders->start_client_id, ClientID{1});
    EXPECT_EQ(config.noise_traders->base_seed, 100ULL);
    EXPECT_EQ(config.noise_traders->initial_wakeup_start, Timestamp{5});
    EXPECT_EQ(config.noise_traders->initial_wakeup_step, Timestamp{10});
}

TEST(ConfigLoaderTest, SimulationConfigWithAgentsAndOrders) {
    json j = {
        {"agents", {
            {
                {"client_id", 1},
                {"type", "NoiseTrader"},
                {"initial_wakeup", 10},
                {"seed", 100},
                {"config", {
                    {"instrument", 1},
                    {"observation_noise", 50.0},
                    {"spread", 36},
                    {"min_quantity", 10},
                    {"max_quantity", 100},
                    {"min_interval", 50},
                    {"max_interval", 200},
                    {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
                }}
            }
        }},
        {"initial_orders", {
            {
                {"instrument", 1},
                {"side", "BUY"},
                {"price", 999900},
                {"quantity", 500}
            }
        }}
    };

    SimulationConfig config = j.get<SimulationConfig>();

    EXPECT_EQ(config.agents.size(), 1ULL);
    EXPECT_EQ(config.agents[0].type, "NoiseTrader");
    EXPECT_EQ(config.initial_orders.size(), 1ULL);
    EXPECT_EQ(config.initial_orders[0].side, OrderSide::BUY);
}

// =============================================================================
// load_config Error Cases
// =============================================================================

TEST(ConfigLoaderTest, LoadConfigNonexistentFileThrows) {
    EXPECT_THROW(load_config("/nonexistent/path/config.json"), std::runtime_error);
}

// =============================================================================
// Per-Agent Latency
// =============================================================================

TEST(ConfigLoaderTest, ParseAgentConfigWithLatency) {
    json j = {
        {"client_id", 1},
        {"type", "NoiseTrader"},
        {"initial_wakeup", 10},
        {"latency", 25},
        {"seed", 100},
        {"config", {
            {"instrument", 1},
            {"observation_noise", 50.0},
            {"spread", 36},
            {"min_quantity", 10},
            {"max_quantity", 100},
            {"min_interval", 50},
            {"max_interval", 200},
            {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
        }}
    };

    AgentConfig config = j.get<AgentConfig>();

    EXPECT_EQ(config.id, ClientID{1});
    EXPECT_EQ(config.latency, Timestamp{25});
}

TEST(ConfigLoaderTest, ParseAgentConfigWithoutLatencyDefaultsToZero) {
    json j = {
        {"client_id", 1},
        {"type", "NoiseTrader"},
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {
            {"instrument", 1},
            {"observation_noise", 50.0},
            {"spread", 36},
            {"min_quantity", 10},
            {"max_quantity", 100},
            {"min_interval", 50},
            {"max_interval", 200},
            {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
        }}
    };

    AgentConfig config = j.get<AgentConfig>();

    EXPECT_EQ(config.latency, Timestamp{0});
}

// =============================================================================
// Garbage Input Tests - Wrong Types
// =============================================================================

TEST(ConfigLoaderGarbageTest, StringWhereNumberExpected) {
    json j = {
        {"initial_price", "not_a_number"},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    EXPECT_THROW(j.get<FairPriceConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, NumberWhereStringExpected) {
    json j = {
        {"client_id", 1},
        {"type", 12345},  // Should be a string
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    EXPECT_THROW(j.get<AgentConfig>(), json::type_error);
}

TEST(ConfigLoaderGarbageTest, ArrayWhereObjectExpected) {
    json j = json::array({1, 2, 3});  // config expects an object

    EXPECT_THROW(j.get<FairPriceConfig>(), json::type_error);
}

TEST(ConfigLoaderGarbageTest, ObjectWhereArrayExpected) {
    json j = {
        {"instruments", {{"not", "an_array"}}}  // Should be array of numbers
    };

    EXPECT_THROW(j.get<SimulationConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, BooleanWhereNumberExpected) {
    json j = {
        {"initial_price", true},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    EXPECT_THROW(j.get<FairPriceConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, NullWhereNumberExpected) {
    json j = {
        {"initial_price", nullptr},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    EXPECT_THROW(j.get<FairPriceConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, NullWhereStringExpected) {
    json j = {
        {"client_id", 1},
        {"type", nullptr},
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    EXPECT_THROW(j.get<AgentConfig>(), json::type_error);
}

TEST(ConfigLoaderGarbageTest, ObjectWhereNumberExpected) {
    json j = {
        {"initial_price", {{"nested", "object"}}},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    EXPECT_THROW(j.get<FairPriceConfig>(), std::runtime_error);
}

// =============================================================================
// Garbage Input Tests - Invalid Numeric Values
// =============================================================================

TEST(ConfigLoaderGarbageTest, NegativeNumberForUnsigned) {
    json j = {
        {"initial_price", -1000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    EXPECT_THROW(j.get<FairPriceConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, FloatingPointWhereIntegerExpected) {
    json j = {
        {"initial_price", 1000.5},  // Should be integer
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    // JSON allows this - float gets truncated. This is expected behavior.
    FairPriceConfig config = j.get<FairPriceConfig>();
    EXPECT_EQ(config.initial_price, Price{1000});
}

TEST(ConfigLoaderGarbageTest, ExtremelyLargeNumber) {
    // Use a float clearly exceeding uint64_t max (avoids floating point edge cases)
    json j = {
        {"initial_price", 1e25},  // ~10^25, way larger than UINT64_MAX (~1.8e19)
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    EXPECT_THROW(j.get<FairPriceConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, NegativeQuantity) {
    json j = {
        {"instrument", 1},
        {"observation_noise", 50.0},
        {"spread", 36},
        {"min_quantity", -10},  // Negative
        {"max_quantity", 100},
        {"min_interval", 50},
        {"max_interval", 200},
        {"adverse_fill_threshold", 100},
        {"stale_order_threshold", 1000}
    };

    EXPECT_THROW(j.get<NoiseTraderConfig>(), std::runtime_error);
}

// =============================================================================
// Garbage Input Tests - Invalid String Values
// =============================================================================

TEST(ConfigLoaderGarbageTest, EmptyAgentType) {
    json j = {
        {"client_id", 1},
        {"type", ""},  // Empty string
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    // Empty type is unknown, should throw runtime_error
    EXPECT_THROW(j.get<AgentConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, InvalidOrderSide) {
    json j = {
        {"instrument", 1},
        {"side", "INVALID"},  // Not BUY or SELL
        {"price", 1000},
        {"quantity", 100}
    };

    // Current code defaults to SELL for any non-"BUY" string
    InitialOrder order = j.get<InitialOrder>();
    EXPECT_EQ(order.side, OrderSide::SELL);  // Documents current behavior
}

TEST(ConfigLoaderGarbageTest, InvalidFairPriceModel) {
    json j = {
        {"model", "invalid_model"},
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
    };

    // Unknown model defaults to GBM
    FairPriceModelConfig config = parse_fair_price_config(j);
    EXPECT_TRUE(std::holds_alternative<FairPriceConfig>(config));
}

TEST(ConfigLoaderGarbageTest, WhitespaceAgentType) {
    json j = {
        {"client_id", 1},
        {"type", "   "},  // Whitespace only
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    EXPECT_THROW(j.get<AgentConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, MixedCaseAgentType) {
    json j = {
        {"client_id", 1},
        {"type", "noisetrader"},  // Wrong case
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    EXPECT_THROW(j.get<AgentConfig>(), std::runtime_error);
}

// =============================================================================
// Garbage Input Tests - Missing Required Fields
// =============================================================================

TEST(ConfigLoaderGarbageTest, NoiseTraderConfigMissingField) {
    json j = {
        {"instrument", 1},
        {"observation_noise", 50.0},
        {"spread", 36}
        // Missing all other required fields
    };

    EXPECT_THROW(j.get<NoiseTraderConfig>(), json::out_of_range);
}

TEST(ConfigLoaderGarbageTest, MarketMakerConfigMissingField) {
    json j = {
        {"instrument", 1},
        {"observation_noise", 10.0}
        // Missing half_spread, quote_size, etc.
    };

    EXPECT_THROW(j.get<MarketMakerConfig>(), json::out_of_range);
}

TEST(ConfigLoaderGarbageTest, InformedTraderConfigMissingField) {
    json j = {
        {"instrument", 1}
        // Missing all other required fields
    };

    EXPECT_THROW(j.get<InformedTraderConfig>(), json::out_of_range);
}

TEST(ConfigLoaderGarbageTest, JumpDiffusionConfigMissingJumpParams) {
    json j = {
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
        // Missing jump_intensity, jump_mean, jump_std
    };

    EXPECT_THROW(j.get<JumpDiffusionConfig>(), json::out_of_range);
}

TEST(ConfigLoaderGarbageTest, AgentConfigMissingClientId) {
    json j = {
        // Missing client_id
        {"type", "NoiseTrader"},
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {
            {"instrument", 1},
            {"observation_noise", 50.0},
            {"spread", 36},
            {"min_quantity", 10},
            {"max_quantity", 100},
            {"min_interval", 50},
            {"max_interval", 200},
            {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
        }}
    };

    EXPECT_THROW(j.get<AgentConfig>(), json::out_of_range);
}

TEST(ConfigLoaderGarbageTest, AgentConfigMissingConfig) {
    json j = {
        {"client_id", 1},
        {"type", "NoiseTrader"},
        {"initial_wakeup", 10},
        {"seed", 100}
        // Missing "config" field
    };

    EXPECT_THROW(j.get<AgentConfig>(), json::out_of_range);
}

TEST(ConfigLoaderGarbageTest, InitialOrderMissingPrice) {
    json j = {
        {"instrument", 1},
        {"side", "BUY"},
        // Missing price
        {"quantity", 100}
    };

    EXPECT_THROW(j.get<InitialOrder>(), json::out_of_range);
}

TEST(ConfigLoaderGarbageTest, NoiseTraderGroupMissingConfig) {
    json j = {
        {"count", 10},
        {"start_client_id", 1},
        {"base_seed", 100},
        {"initial_wakeup_start", 5},
        {"initial_wakeup_step", 10}
        // Missing "config" field
    };

    EXPECT_THROW(j.get<NoiseTraderGroupConfig>(), json::out_of_range);
}

// =============================================================================
// Garbage Input Tests - Nested Invalid Data
// =============================================================================

TEST(ConfigLoaderGarbageTest, NestedGarbageInAgentConfig) {
    json j = {
        {"client_id", 1},
        {"type", "NoiseTrader"},
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {
            {"instrument", "not_a_number"},  // Wrong type in nested config
            {"observation_noise", 50.0},
            {"spread", 36},
            {"min_quantity", 10},
            {"max_quantity", 100},
            {"min_interval", 50},
            {"max_interval", 200},
            {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
        }}
    };

    EXPECT_THROW(j.get<AgentConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, NestedGarbageInNoiseTraderGroup) {
    json j = {
        {"count", 10},
        {"start_client_id", 1},
        {"base_seed", 100},
        {"initial_wakeup_start", 5},
        {"initial_wakeup_step", 10},
        {"config", {
            {"instrument", 1},
            {"observation_noise", "garbage"},  // Wrong type
            {"spread", 36},
            {"min_quantity", 10},
            {"max_quantity", 100},
            {"min_interval", 50},
            {"max_interval", 200},
            {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
        }}
    };

    EXPECT_THROW(j.get<NoiseTraderGroupConfig>(), json::type_error);
}

TEST(ConfigLoaderGarbageTest, SimulationConfigWithGarbageAgents) {
    json j = {
        {"agents", {
            {
                {"client_id", "not_a_number"},  // Wrong type
                {"type", "NoiseTrader"},
                {"initial_wakeup", 10},
                {"seed", 100},
                {"config", {}}
            }
        }}
    };

    EXPECT_THROW(j.get<SimulationConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, SimulationConfigWithGarbageInstruments) {
    json j = {
        {"instruments", {"a", "b", "c"}}  // Strings instead of numbers
    };

    EXPECT_THROW(j.get<SimulationConfig>(), std::runtime_error);
}

// =============================================================================
// Garbage Input Tests - Completely Invalid JSON Structures
// =============================================================================

TEST(ConfigLoaderGarbageTest, PrimitiveInsteadOfObject) {
    json j = 12345;  // Just a number, not an object

    EXPECT_THROW(j.get<SimulationConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, NullJson) {
    json j = nullptr;

    EXPECT_THROW(j.get<SimulationConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, StringInsteadOfObject) {
    json j = "this is just a string";

    EXPECT_THROW(j.get<SimulationConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, ArrayInsteadOfSimulationConfig) {
    json j = json::array({1, 2, 3, 4, 5});

    EXPECT_THROW(j.get<SimulationConfig>(), std::runtime_error);
}

// =============================================================================
// Garbage Input Tests - Edge Cases
// =============================================================================

TEST(ConfigLoaderGarbageTest, ZeroCount) {
    json j = {
        {"count", 0},  // Zero traders
        {"start_client_id", 1},
        {"base_seed", 100},
        {"initial_wakeup_start", 5},
        {"initial_wakeup_step", 10},
        {"config", {
            {"instrument", 1},
            {"observation_noise", 50.0},
            {"spread", 36},
            {"min_quantity", 10},
            {"max_quantity", 100},
            {"min_interval", 50},
            {"max_interval", 200},
            {"adverse_fill_threshold", 100},
            {"stale_order_threshold", 1000}
        }}
    };

    // Zero count should parse (semantic validation is separate)
    NoiseTraderGroupConfig config = j.get<NoiseTraderGroupConfig>();
    EXPECT_EQ(config.count, 0ULL);
}

TEST(ConfigLoaderGarbageTest, ZeroQuantityRange) {
    json j = {
        {"instrument", 1},
        {"observation_noise", 50.0},
        {"spread", 36},
        {"min_quantity", 100},
        {"max_quantity", 10},  // min > max (invalid semantically)
        {"min_interval", 50},
        {"max_interval", 200},
        {"adverse_fill_threshold", 100},
        {"stale_order_threshold", 1000}
    };

    // Should parse successfully (semantic validation is separate)
    NoiseTraderConfig config = j.get<NoiseTraderConfig>();
    EXPECT_EQ(config.min_quantity, Quantity{100});
    EXPECT_EQ(config.max_quantity, Quantity{10});
}

TEST(ConfigLoaderGarbageTest, VeryLongString) {
    std::string long_string(10000, 'x');
    json j = {
        {"client_id", 1},
        {"type", long_string},
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    // Should throw runtime_error for unknown agent type
    EXPECT_THROW(j.get<AgentConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, SpecialCharactersInType) {
    json j = {
        {"client_id", 1},
        {"type", "Noise\nTrader\0Test"},  // Embedded newline and null
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    EXPECT_THROW(j.get<AgentConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, UnicodeInType) {
    json j = {
        {"client_id", 1},
        {"type", "NoiseTrader\xF0\x9F\x92\xB0"},  // With emoji
        {"initial_wakeup", 10},
        {"seed", 100},
        {"config", {}}
    };

    EXPECT_THROW(j.get<AgentConfig>(), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, DuplicateKeys) {
    // JSON spec says behavior is undefined, nlohmann::json uses last value
    std::string json_str = R"({
        "initial_price": 1000,
        "initial_price": 2000,
        "drift": 0.0001,
        "volatility": 0.005,
        "tick_size": 1000
    })";

    json j = json::parse(json_str);
    FairPriceConfig config = j.get<FairPriceConfig>();
    EXPECT_EQ(config.initial_price, Price{2000});  // Last value wins
}

TEST(ConfigLoaderGarbageTest, EmptyAgentsArray) {
    json j = {
        {"agents", json::array()}
    };

    SimulationConfig config = j.get<SimulationConfig>();
    EXPECT_TRUE(config.agents.empty());
}

TEST(ConfigLoaderGarbageTest, DeepNestedGarbage) {
    json j = {
        {"simulation", {
            {"latency", {{"deeply", {{"nested", "garbage"}}}}}  // Object where number expected
        }}
    };

    EXPECT_THROW(j.get<SimulationConfig>(), std::runtime_error);
}

// =============================================================================
// Fair Price Model Mismatch Tests
// =============================================================================

TEST(ConfigLoaderGarbageTest, JumpDiffusionModelWithGBMParams) {
    // Specifying model="jump_diffusion" but only providing GBM params
    // should throw because jump params are missing
    json j = {
        {"model", "jump_diffusion"},
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000}
        // Missing: jump_intensity, jump_mean, jump_std
    };

    EXPECT_THROW(parse_fair_price_config(j), json::out_of_range);
}

TEST(ConfigLoaderGarbageTest, GBMModelWithJumpDiffusionParams) {
    // Specifying model="gbm" but providing jump_diffusion params
    // should throw because jump params are extraneous for GBM
    json j = {
        {"model", "gbm"},
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000},
        {"jump_intensity", 0.1},
        {"jump_mean", 0.0},
        {"jump_std", 0.05}
    };

    EXPECT_THROW(parse_fair_price_config(j), std::runtime_error);
}

TEST(ConfigLoaderGarbageTest, DefaultModelWithJumpDiffusionParams) {
    // No model specified (defaults to GBM) but providing jump_diffusion params
    // should throw because jump params are extraneous for GBM
    json j = {
        {"initial_price", 1000000},
        {"drift", 0.0001},
        {"volatility", 0.005},
        {"tick_size", 1000},
        {"jump_intensity", 0.1},
        {"jump_mean", 0.0},
        {"jump_std", 0.05}
    };

    EXPECT_THROW(parse_fair_price_config(j), std::runtime_error);
}
