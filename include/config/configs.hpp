#pragma once

#include "exchange/types.hpp"
#include "utils/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

/**
 * Configuration for the fair price generator using Geometric Brownian Motion.
 * Controls the evolution of the true underlying asset price over time.
 */
struct FairPriceConfig {
    Price initial_price;
    double drift;
    double volatility;
    Timestamp tick_size;
};

/**
 * Configuration for the fair price generator using Merton's Jump Diffusion Model.
 * Extends GBM with random jumps that arrive according to a Poisson process.
 * Jump sizes are log-normally distributed.
 *
 * Parameters:
 * - initial_price: Starting price of the asset
 * - drift: Expected return (mu)
 * - volatility: Diffusion volatility (sigma)
 * - tick_size: Time unit for dt calculation
 * - jump_intensity: Mean number of jumps per tick (lambda)
 * - jump_mean: Mean of log-jump sizes (mu_J)
 * - jump_std: Standard deviation of log-jump sizes (sigma_J)
 */
struct JumpDiffusionConfig {
    Price initial_price;
    double drift;
    double volatility;
    Timestamp tick_size;
    double jump_intensity;
    double jump_mean;
    double jump_std;
};

/**
 * Variant type for fair price model configuration.
 * Supports GBM (FairPriceConfig) and Jump Diffusion (JumpDiffusionConfig).
 */
using FairPriceModelConfig = std::variant<FairPriceConfig, JumpDiffusionConfig>;

/**
 * Configuration for noise traders that provide random liquidity.
 * Noise traders observe the fair price with noise and submit random limit orders
 * around it with configurable spread, quantity bounds, and wakeup intervals.
 *
 * Order cancellation thresholds:
 * - adverse_fill_threshold: Cancel orders that would result in a bad fill
 *   (BUY too high above fair, SELL too low below fair)
 * - stale_order_threshold: Cancel orders too far from fair to ever execute
 *   (BUY too far below fair, SELL too far above fair)
 *
 * Latency jitter: sigma parameter for log-normal latency jitter; 0 = no jitter.
 * When > 0, each action's latency is sampled from LogNormal(ln(base_latency), sigma)
 * so the median equals the base latency.
 */
struct NoiseTraderConfig {
    InstrumentID instrument;
    double observation_noise;
    Price spread;
    Quantity min_quantity;
    Quantity max_quantity;
    Timestamp min_interval;
    Timestamp max_interval;
    Price adverse_fill_threshold;
    Price stale_order_threshold;
    double latency_jitter{0.0};
};

/**
 * Configuration for generating multiple noise traders with shared parameters.
 * Traders are created with sequential client IDs, staggered wakeups, and
 * deterministic seeds derived from a base seed.
 */
struct NoiseTraderGroupConfig {
    std::uint64_t count;
    ClientID start_client_id;
    std::uint64_t base_seed;
    Timestamp initial_wakeup_start;
    Timestamp initial_wakeup_step;
    NoiseTraderConfig config;
};

/**
 * Configuration for market makers that quote on both sides of the book.
 * Market makers observe the fair price with noise and adjust their quotes based
 * on inventory position using a skew factor, respecting maximum position limits.
 *
 * Latency jitter: sigma parameter for log-normal latency jitter; 0 = no jitter.
 * When > 0, each action's latency is sampled from LogNormal(ln(base_latency), sigma)
 * so the median equals the base latency.
 */
struct MarketMakerConfig {
    InstrumentID instrument;
    double observation_noise;
    Price half_spread;
    Quantity quote_size;
    Timestamp update_interval;
    double inventory_skew_factor;
    Quantity max_position;
    double latency_jitter{0.0};
};

/**
 * Configuration for informed traders that trade based on fair price observations.
 * Informed traders observe the fair price with noise and trade when they detect
 * sufficient edge relative to the current order book.
 *
 * Order cancellation thresholds:
 * - adverse_fill_threshold: Cancel orders that would result in a bad fill
 *   (BUY too high above fair, SELL too low below fair)
 * - stale_order_threshold: Cancel orders too far from fair to ever execute
 *   (BUY too far below fair, SELL too far above fair)
 *
 * Latency jitter: sigma parameter for log-normal latency jitter; 0 = no jitter.
 * When > 0, each action's latency is sampled from LogNormal(ln(base_latency), sigma)
 * so the median equals the base latency.
 */
struct InformedTraderConfig {
    InstrumentID instrument;
    Quantity min_quantity;
    Quantity max_quantity;
    Timestamp min_interval;
    Timestamp max_interval;
    Price min_edge;
    double observation_noise;
    Price adverse_fill_threshold;
    Price stale_order_threshold;
    double latency_jitter{0.0};
};

/**
 * Configuration for a single agent instance.
 * Contains the agent type, identifier, and type-specific configuration.
 *
 * Latency: per-agent base latency; 0 means use global default.
 * Latency jitter is configured per agent type in the type-specific config.
 */
struct AgentConfig {
    ClientID id;
    std::string type;
    std::uint64_t seed;
    Timestamp initial_wakeup;
    Timestamp latency{0};
    NoiseTraderConfig noise_trader;
    MarketMakerConfig market_maker;
    InformedTraderConfig informed_trader;
};

/**
 * Configuration for an initial order used to seed the order book.
 * These orders are placed at timestamp 0 before the simulation begins.
 */
struct InitialOrder {
    InstrumentID instrument;
    OrderSide side;
    Price price;
    Quantity quantity;
};

/**
 * Complete simulation configuration loaded from a JSON file.
 * Contains all parameters needed to run a simulation including simulation settings,
 * instruments, fair price dynamics, agent configurations, and initial orders.
 */
struct SimulationConfig {
    Timestamp latency{0};
    Timestamp duration{1000};
    std::filesystem::path output_dir{"./output"};
    Timestamp pnl_snapshot_interval{100};
    std::vector<InstrumentID> instruments;
    FairPriceModelConfig fair_price;
    std::uint64_t fair_price_seed{0};
    std::optional<NoiseTraderGroupConfig> noise_traders;
    std::vector<AgentConfig> agents;
    std::vector<InitialOrder> initial_orders;
};
