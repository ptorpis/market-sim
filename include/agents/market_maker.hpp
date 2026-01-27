#pragma once

#include "simulation/agent.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <random>

struct MarketMakerConfig {
    InstrumentID instrument;
    Price half_spread;
    Quantity quote_size;
    Timestamp update_interval;
    double inventory_skew_factor;
    Quantity max_position;
};

/**
 * Quotes bid/ask around the order book midpoint with inventory-based skew.
 * Tracks position from fills and adjusts quotes to reduce inventory risk.
 */
class MarketMaker : public Agent {
public:
    MarketMaker(ClientID id, MarketMakerConfig config, std::uint64_t seed)
        : Agent(id), config_(config), rng_(seed) {}

    void on_wakeup(AgentContext& ctx) override {
        cancel_existing_quotes(ctx);
        post_new_quotes(ctx);
        ctx.schedule_wakeup(ctx.now() + config_.update_interval);
    }

    void on_trade([[maybe_unused]] AgentContext& ctx, const Trade& trade) override {
        if (trade.buyer_id == id()) {
            long_position_ = long_position_ + trade.quantity;
        }
        if (trade.seller_id == id()) {
            short_position_ = short_position_ + trade.quantity;
        }
    }

    void on_order_accepted([[maybe_unused]] AgentContext& ctx,
                           const OrderAccepted& event) override {
        if (event.agent_id == id()) {
            active_orders_.push_back(event.order_id);
        }
    }

    void on_order_cancelled([[maybe_unused]] AgentContext& ctx,
                            const OrderCancelled& event) override {
        std::erase(active_orders_, event.order_id);
    }

    [[nodiscard]] Quantity long_position() const { return long_position_; }
    [[nodiscard]] Quantity short_position() const { return short_position_; }

    [[nodiscard]] std::int64_t net_position() const {
        return static_cast<std::int64_t>(long_position_.value()) -
               static_cast<std::int64_t>(short_position_.value());
    }

private:
    MarketMakerConfig config_;
    std::mt19937_64 rng_;
    Quantity long_position_{0};
    Quantity short_position_{0};
    std::vector<OrderID> active_orders_;

    void cancel_existing_quotes(AgentContext& ctx) {
        for (OrderID order_id : active_orders_) {
            ctx.cancel_order(order_id);
        }
        active_orders_.clear();
    }

    void post_new_quotes(AgentContext& ctx) {
        const auto& book = ctx.get_order_book(config_.instrument);

        auto midpoint = calculate_midpoint(book);
        if (!midpoint.has_value()) {
            return;
        }

        double mid = static_cast<double>(midpoint->value());
        double half = static_cast<double>(config_.half_spread.value());
        double skew = static_cast<double>(net_position()) * config_.inventory_skew_factor;

        double bid = std::max(1.0, mid - half - skew);
        double ask = std::max(1.0, mid + half - skew);

        auto net = net_position();
        auto max = static_cast<std::int64_t>(config_.max_position.value());

        if (net < max) {
            ctx.submit_order(config_.instrument, config_.quote_size,
                             Price{static_cast<std::uint64_t>(std::round(bid))}, OrderSide::BUY,
                             OrderType::LIMIT);
        }

        if (net > -max) {
            ctx.submit_order(config_.instrument, config_.quote_size,
                             Price{static_cast<std::uint64_t>(std::round(ask))}, OrderSide::SELL,
                             OrderType::LIMIT);
        }
    }

    [[nodiscard]] std::optional<Price> calculate_midpoint(const OrderBook& book) const {
        if (book.bids.empty() || book.asks.empty()) {
            return std::nullopt;
        }

        Price best_bid = book.bids.begin()->first;
        Price best_ask = book.asks.begin()->first;

        return Price{(best_bid.value() + best_ask.value()) / 2};
    }
};
