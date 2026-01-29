#pragma once

#include "config/configs.hpp"

#include <cmath>
#include <random>

/**
 * Generates fair prices using Geometric Brownian Motion.
 */
class FairPriceGenerator {
public:
    FairPriceGenerator(FairPriceConfig config, std::uint64_t seed)
        : config_(config),
          current_price_(static_cast<double>(config.initial_price.value())),
          last_update_(Timestamp{0}),
          rng_(seed) {}

    void advance_to(Timestamp t) {
        if (t <= last_update_) {
            return;
        }

        auto dt = static_cast<double>((t - last_update_).value()) /
                  static_cast<double>(config_.tick_size.value());

        std::normal_distribution<double> dist(0.0, 1.0);
        double z = dist(rng_);

        double drift_term = (config_.drift - 0.5 * config_.volatility * config_.volatility) * dt;
        double diffusion_term = config_.volatility * std::sqrt(dt) * z;

        current_price_ *= std::exp(drift_term + diffusion_term);
        last_update_ = t;
    }

    [[nodiscard]] Price true_price() const {
        return Price{static_cast<std::uint64_t>(std::round(current_price_))};
    }

    [[nodiscard]] Timestamp last_update() const { return last_update_; }

    [[nodiscard]] const FairPriceConfig& config() const { return config_; }

private:
    FairPriceConfig config_;
    double current_price_;
    Timestamp last_update_;
    std::mt19937_64 rng_;
};
