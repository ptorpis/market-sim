#pragma once

#include "simulation/agent.hpp"

#include <random>

struct InformedTraderConfig {
    InstrumentID instrument;
    Quantity min_quantity;
    Quantity max_quantity;
    Timestamp min_interval;
    Timestamp max_interval;
    Price min_edge;
};

/**
 * Trades based on a noisy observation of the fair price.
 * Buys when observed fair price > best ask + min_edge.
 * Sells when observed fair price < best bid - min_edge.
 */
class InformedTrader : public Agent {
public:
    InformedTrader(ClientID id, InformedTraderConfig config, std::uint64_t seed)
        : Agent(id), config_(config), rng_(seed), observation_seed_(seed) {}

    void on_wakeup(AgentContext& ctx) override {
        Price observed = ctx.observe_fair_price(observation_seed_);
        const auto& book = ctx.get_order_book(config_.instrument);

        if (!book.asks.empty()) {
            Price best_ask = book.asks.begin()->first;
            if (observed > best_ask + config_.min_edge) {
                Quantity qty = random_quantity();
                ctx.submit_order(config_.instrument, qty, best_ask, OrderSide::BUY,
                                 OrderType::LIMIT);
            }
        }

        if (!book.bids.empty()) {
            Price best_bid = book.bids.begin()->first;
            if (observed + config_.min_edge < best_bid) {
                Quantity qty = random_quantity();
                ctx.submit_order(config_.instrument, qty, best_bid, OrderSide::SELL,
                                 OrderType::LIMIT);
            }
        }

        schedule_next_wakeup(ctx);
    }

private:
    InformedTraderConfig config_;
    std::mt19937_64 rng_;
    std::uint64_t observation_seed_;

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
