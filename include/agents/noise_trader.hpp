#pragma once

#include "simulation/agent.hpp"

#include <random>

struct NoiseTraderConfig {
    InstrumentID instrument;
    Price fair_value;
    Price spread;            // Orders placed within [fair_value - spread, fair_value + spread]
    Quantity min_quantity;
    Quantity max_quantity;
    Timestamp min_interval;  // Min time between wakeups
    Timestamp max_interval;  // Max time between wakeups
};

class NoiseTrader : public Agent {
public:
    NoiseTrader(ClientID id, NoiseTraderConfig config, std::uint64_t seed)
        : Agent(id), config_(config), rng_(seed) {}

    void on_wakeup(AgentContext& ctx) override {
        submit_random_order(ctx);
        schedule_next_wakeup(ctx);
    }

private:
    NoiseTraderConfig config_;
    std::mt19937_64 rng_;

    void submit_random_order(AgentContext& ctx) {
        std::uniform_int_distribution<int> side_dist(0, 1);
        OrderSide side = side_dist(rng_) == 0 ? OrderSide::BUY : OrderSide::SELL;

        std::uniform_int_distribution<std::uint64_t> price_dist(
            config_.fair_value.value() - config_.spread.value(),
            config_.fair_value.value() + config_.spread.value());
        Price price{price_dist(rng_)};

        std::uniform_int_distribution<std::uint64_t> qty_dist(
            config_.min_quantity.value(), config_.max_quantity.value());
        Quantity quantity{qty_dist(rng_)};

        ctx.submit_order(config_.instrument, quantity, price, side, OrderType::LIMIT);
    }

    void schedule_next_wakeup(AgentContext& ctx) {
        std::uniform_int_distribution<std::uint64_t> interval_dist(
            config_.min_interval.value(), config_.max_interval.value());
        Timestamp next = ctx.now() + Timestamp{interval_dist(rng_)};
        ctx.schedule_wakeup(next);
    }
};
