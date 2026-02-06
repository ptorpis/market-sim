#pragma once

#include "agents/tracked_order.hpp"
#include "config/configs.hpp"
#include "simulation/agent.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <vector>

// Forward declaration for test access
class InformedTraderTestAccess;

/**
 * Trades based on a noisy observation of the fair price.
 * Buys when observed fair price > best ask + min_edge.
 * Sells when observed fair price < best bid - min_edge.
 * Cancels orders when fair price deviates beyond stale_order_threshold.
 */
class InformedTrader : public Agent {
    friend class InformedTraderTestAccess;

public:
    InformedTrader(ClientID id, InformedTraderConfig config, std::uint64_t seed)
        : Agent(id), config_(config), rng_(seed) {}

    void on_wakeup(AgentContext& ctx) override {
        cancel_stale_orders(ctx);

        Price observed = observe_price(ctx);
        const auto& book = ctx.get_order_book(config_.instrument);

        if (!book.asks.empty()) {
            Price best_ask = book.asks.begin()->first;
            if (observed > best_ask + config_.min_edge) {
                Quantity qty = random_quantity();
                pending_submissions_.push({best_ask, OrderSide::BUY, qty});
                ctx.submit_order(config_.instrument, qty, best_ask, OrderSide::BUY,
                                 OrderType::LIMIT);
            }
        }

        if (!book.bids.empty()) {
            Price best_bid = book.bids.begin()->first;
            if (observed + config_.min_edge < best_bid) {
                Quantity qty = random_quantity();
                pending_submissions_.push({best_bid, OrderSide::SELL, qty});
                ctx.submit_order(config_.instrument, qty, best_bid, OrderSide::SELL,
                                 OrderType::LIMIT);
            }
        }

        schedule_next_wakeup(ctx);
    }

    void on_order_accepted([[maybe_unused]] AgentContext& ctx,
                           const OrderAccepted& event) override {
        if (event.agent_id == id() && !pending_submissions_.empty()) {
            auto pending = pending_submissions_.front();
            pending_submissions_.pop();
            active_orders_.push_back(
                {event.order_id, pending.price, pending.side, pending.quantity});
        }
    }

    void on_order_cancelled([[maybe_unused]] AgentContext& ctx,
                            const OrderCancelled& event) override {
        std::erase_if(active_orders_, [&](const auto& o) {
            return o.order_id == event.order_id;
        });
    }

    void on_trade([[maybe_unused]] AgentContext& ctx, const Trade& trade) override {
        auto update_order = [&](OrderID order_id) {
            auto it = std::find_if(active_orders_.begin(), active_orders_.end(),
                                   [&](const auto& o) {
                                       return o.order_id == order_id;
                                   });
            if (it != active_orders_.end()) {
                if (trade.quantity >= it->remaining_quantity) {
                    active_orders_.erase(it);
                } else {
                    it->remaining_quantity = it->remaining_quantity - trade.quantity;
                }
            }
        };

        if (trade.buyer_id == id()) {
            update_order(trade.buyer_order_id);
        }
        if (trade.seller_id == id()) {
            update_order(trade.seller_order_id);
        }
    }

private:
    InformedTraderConfig config_;
    std::mt19937_64 rng_;
    std::queue<PendingOrder> pending_submissions_;
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
        if (order.side == OrderSide::BUY) {
            // BUY: adverse fill if bidding too far ABOVE fair (would overpay)
            if (!config_.adverse_fill_threshold.is_zero() &&
                order.price > fair + config_.adverse_fill_threshold) {
                return true;
            }
            // BUY: stale if bidding too far BELOW fair (won't execute)
            if (!config_.stale_order_threshold.is_zero() &&
                order.price + config_.stale_order_threshold < fair) {
                return true;
            }
        } else {
            // SELL: adverse fill if asking too far BELOW fair (would undersell)
            if (!config_.adverse_fill_threshold.is_zero() &&
                order.price + config_.adverse_fill_threshold < fair) {
                return true;
            }
            // SELL: stale if asking too far ABOVE fair (won't execute)
            if (!config_.stale_order_threshold.is_zero() &&
                order.price > fair + config_.stale_order_threshold) {
                return true;
            }
        }
        return false;
    }

    void cancel_stale_orders(AgentContext& ctx) {
        Price fair = ctx.fair_price();
        for (const auto& order : active_orders_) {
            if (is_order_stale(order, fair)) {
                ctx.cancel_order(order.order_id);
            }
        }
    }

    Quantity random_quantity() {
        std::uniform_int_distribution<std::uint64_t> dist(config_.min_quantity.value(),
                                                          config_.max_quantity.value());
        return Quantity{dist(rng_)};
    }

    void schedule_next_wakeup(AgentContext& ctx) {
        std::uniform_int_distribution<std::uint64_t> dist(config_.min_interval.value(),
                                                          config_.max_interval.value());
        ctx.schedule_wakeup(ctx.now() + Timestamp{dist(rng_)});
    }
};
