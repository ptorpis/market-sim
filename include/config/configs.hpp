#pragma once

#include "exchange/types.hpp"
#include "utils/types.hpp"

#include <filesystem>
#include <string>
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
 * Configuration for noise traders that provide random liquidity.
 * Noise traders observe the fair price with noise and submit random limit orders
 * around it with configurable spread, quantity bounds, and wakeup intervals.
 *
 * Order cancellation thresholds:
 * - adverse_fill_threshold: Cancel orders that would result in a bad fill
 *   (BUY too high above fair, SELL too low below fair)
 * - stale_order_threshold: Cancel orders too far from fair to ever execute
 *   (BUY too far below fair, SELL too far above fair)
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
};

/**
 * Configuration for market makers that quote on both sides of the book.
 * Market makers observe the fair price with noise and adjust their quotes based
 * on inventory position using a skew factor, respecting maximum position limits.
 */
struct MarketMakerConfig {
    InstrumentID instrument;
    double observation_noise;
    Price half_spread;
    Quantity quote_size;
    Timestamp update_interval;
    double inventory_skew_factor;
    Quantity max_position;
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
};

/**
 * Configuration for a single agent instance.
 * Contains the agent type, identifier, and type-specific configuration.
 */
struct AgentConfig {
    ClientID id;
    std::string type;
    std::uint64_t seed;
    Timestamp initial_wakeup;
    Timestamp latency{0}; // Per-agent latency; 0 means use global default
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
    FairPriceConfig fair_price;
    std::uint64_t fair_price_seed{0};
    std::vector<AgentConfig> agents;
    std::vector<InitialOrder> initial_orders;
};
