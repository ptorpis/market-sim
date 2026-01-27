#pragma once

#include "utils/types.hpp"

#include <cmath>
#include <random>

struct FairPriceConfig {
    Price initial_price;
    double drift;
    double volatility;
    double observation_noise;
    Timestamp tick_size;
};

/**
 * Generates fair prices using Geometric Brownian Motion.
 * Agents observe the price with per-agent noise seeded deterministically.
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

    [[nodiscard]] Price observe(std::uint64_t agent_seed) const {
        if (config_.observation_noise <= 0.0) {
            return true_price();
        }

        std::mt19937_64 agent_rng(agent_seed ^ last_update_.value());
        std::normal_distribution<double> noise_dist(0.0, config_.observation_noise);

        double noisy_price = current_price_ + noise_dist(agent_rng);
        return Price{static_cast<std::uint64_t>(std::max(1.0, std::round(noisy_price)))};
    }

    [[nodiscard]] Timestamp last_update() const { return last_update_; }

    [[nodiscard]] const FairPriceConfig& config() const { return config_; }

private:
    FairPriceConfig config_;
    double current_price_;
    Timestamp last_update_;
    std::mt19937_64 rng_;
};
