#pragma once

#include "agents/tracked_order.hpp"
#include "config/configs.hpp"
#include "simulation/agent.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <vector>

/**
 * Submits random limit orders around an observed fair price.
 * The fair price observation includes configurable noise.
 * Cancels orders when fair price deviates beyond stale_order_threshold.
 */
class NoiseTrader : public Agent {
public:
    NoiseTrader(ClientID id, NoiseTraderConfig config, std::uint64_t seed)
        : Agent(id), config_(config), rng_(seed) {}

    void on_wakeup(AgentContext& ctx) override {
        cancel_stale_orders(ctx);
        submit_random_order(ctx);
        schedule_next_wakeup(ctx);
    }

    void on_order_accepted([[maybe_unused]] AgentContext& ctx,
                           const OrderAccepted& event) override {
        if (event.agent_id == id() && !pending_submissions_.empty()) {
            auto [price, side] = pending_submissions_.front();
            pending_submissions_.pop();
            active_orders_.push_back({event.order_id, price, side});
        }
    }

    void on_order_cancelled([[maybe_unused]] AgentContext& ctx,
                            const OrderCancelled& event) override {
        std::erase_if(active_orders_,
                      [&](const auto& o) { return o.order_id == event.order_id; });
    }

    void on_trade([[maybe_unused]] AgentContext& ctx, const Trade& trade) override {
        // Remove fully filled orders (order no longer in book after full fill)
        // For partial fills, the order remains active
        if (trade.buyer_id == id()) {
            std::erase_if(active_orders_,
                          [&](const auto& o) { return o.order_id == trade.buyer_order_id; });
        }
        if (trade.seller_id == id()) {
            std::erase_if(active_orders_,
                          [&](const auto& o) { return o.order_id == trade.seller_order_id; });
        }
    }

private:
    NoiseTraderConfig config_;
    std::mt19937_64 rng_;
    std::queue<std::pair<Price, OrderSide>> pending_submissions_;
    std::vector<TrackedOrder> active_orders_;

    Price observe_price(AgentContext& ctx) {
        Price true_price = ctx.fair_price();
        if (config_.observation_noise <= 0.0) {
            return true_price;
        }

        std::normal_distribution<double> noise_dist(0.0, config_.observation_noise);
        double noisy = static_cast<double>(true_price.value()) + noise_dist(rng_);
        return Price{static_cast<std::uint64_t>(std::max(1.0, std::round(noisy)))};
    }

    [[nodiscard]] bool is_order_stale(const TrackedOrder& order, Price fair) const {
        if (config_.stale_order_threshold.is_zero()) {
            return false;
        }
        // BUY order is stale if we're bidding too far above fair value
        // SELL order is stale if we're asking too far below fair value
        if (order.side == OrderSide::BUY) {
            return order.price > fair + config_.stale_order_threshold;
        } else {
            return order.price + config_.stale_order_threshold < fair;
        }
    }

    void cancel_stale_orders(AgentContext& ctx) {
        Price fair = ctx.fair_price();
        for (const auto& order : active_orders_) {
            if (is_order_stale(order, fair)) {
                ctx.cancel_order(order.order_id);
            }
        }
    }

    void submit_random_order(AgentContext& ctx) {
        Price observed = observe_price(ctx);

        std::uniform_int_distribution<int> side_dist(0, 1);
        OrderSide side = side_dist(rng_) == 0 ? OrderSide::BUY : OrderSide::SELL;

        std::uniform_int_distribution<std::uint64_t> price_dist(
            observed.value() - config_.spread.value(),
            observed.value() + config_.spread.value());
        Price price{price_dist(rng_)};

        std::uniform_int_distribution<std::uint64_t> qty_dist(
            config_.min_quantity.value(), config_.max_quantity.value());
        Quantity quantity{qty_dist(rng_)};

        pending_submissions_.push({price, side});
        ctx.submit_order(config_.instrument, quantity, price, side, OrderType::LIMIT);
    }

    void schedule_next_wakeup(AgentContext& ctx) {
        std::uniform_int_distribution<std::uint64_t> interval_dist(
            config_.min_interval.value(), config_.max_interval.value());
        Timestamp next = ctx.now() + Timestamp{interval_dist(rng_)};
        ctx.schedule_wakeup(next);
    }
};
