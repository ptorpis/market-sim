#include <gtest/gtest.h>

#include "config/config_loader.hpp"

#include <nlohmann/json.hpp>

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
    EXPECT_EQ(config.fair_price.initial_price, Price{1000000});
    EXPECT_EQ(config.fair_price_seed, 43ULL);
}

TEST(ConfigLoaderTest, SimulationConfigWithMinimalFields) {
    json j = {};

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
