#pragma once

#include "config/configs.hpp"

#include <cmath>
#include <random>

/**
 * Interface for fair price sources.
 * Allows injection of different price generation strategies.
 */
class IFairPriceSource {
public:
    virtual ~IFairPriceSource() = default;

    virtual void advance_to(Timestamp t) = 0;
    [[nodiscard]] virtual Price true_price() const = 0;
    [[nodiscard]] virtual Timestamp last_update() const = 0;
};

/**
 * Generates fair prices using Geometric Brownian Motion.
 */
class FairPriceGenerator : public IFairPriceSource {
public:
    FairPriceGenerator(FairPriceConfig config, std::uint64_t seed)
        : config_(config),
          current_price_(static_cast<double>(config.initial_price.value())),
          last_update_(Timestamp{0}), rng_(seed) {}

    void advance_to(Timestamp t) override {
        if (t <= last_update_) {
            return;
        }

        auto dt = static_cast<double>((t - last_update_).value()) /
                  static_cast<double>(config_.tick_size.value());

        if (config_.volatility == 0.0) {
            current_price_ *= std::exp(config_.drift * dt);
            last_update_ = t;
            return;
        }

        std::normal_distribution<double> dist(0.0, 1.0);
        double z = dist(rng_);

        double drift_term =
            (config_.drift - 0.5 * config_.volatility * config_.volatility) * dt;
        double diffusion_term = config_.volatility * std::sqrt(dt) * z;

        current_price_ *= std::exp(drift_term + diffusion_term);
        last_update_ = t;
    }

    [[nodiscard]] Price true_price() const override {
        return Price{static_cast<std::uint64_t>(std::round(current_price_))};
    }

    [[nodiscard]] Timestamp last_update() const override { return last_update_; }

    [[nodiscard]] const FairPriceConfig& config() const { return config_; }

private:
    FairPriceConfig config_;
    double current_price_;
    Timestamp last_update_;
    std::mt19937_64 rng_;
};

/**
 * Generates fair prices using Merton's Jump Diffusion Model.
 * Extends GBM with random jumps for modeling sudden price movements.
 *
 * The model follows: dS/S = (mu - lambda*k)dt + sigma*dW + J*dN
 * where:
 * - dW is a Wiener process (diffusion)
 * - dN is a Poisson process with intensity lambda (jump arrivals)
 * - J = exp(mu_J + sigma_J * Z) - 1 is the jump size
 * - k = E[exp(J)] - 1 compensates for jump risk
 */
class JumpDiffusionFairPriceGenerator : public IFairPriceSource {
public:
    JumpDiffusionFairPriceGenerator(JumpDiffusionConfig config, std::uint64_t seed)
        : config_(config),
          current_price_(static_cast<double>(config.initial_price.value())),
          last_update_(Timestamp{0}), rng_(seed) {}

    void advance_to(Timestamp t) override {
        if (t <= last_update_) {
            return;
        }

        auto dt = static_cast<double>((t - last_update_).value()) /
                  static_cast<double>(config_.tick_size.value());

        /*
            adding early exit for this case, without this, g++ actually fails an assertion
            clang works fine, even without this check

            Note: according to the C++ standard, the precondition for
           std::poisson_distribution(double mean) is that mean > 0.0 (does not allow =)

            This means that passing 0 below is technically UB
        */
        if (config_.volatility == 0.0) {
            current_price_ *= std::exp(config_.drift * dt);
            last_update_ = t;
            return;
        }

        std::normal_distribution<double> normal_dist(0.0, 1.0);

        // Diffusion component (GBM)
        double z = normal_dist(rng_);

        // Jump compensation: k = E[exp(J)] - 1 = exp(mu_J + 0.5*sigma_J^2) - 1
        double k =
            std::exp(config_.jump_mean + 0.5 * config_.jump_std * config_.jump_std) - 1.0;

        // Drift adjusted for jump compensation
        double drift_term =
            (config_.drift - 0.5 * config_.volatility * config_.volatility -
             config_.jump_intensity * k) *
            dt;
        double diffusion_term = config_.volatility * std::sqrt(dt) * z;

        // Jump component: N ~ Poisson(lambda * dt), each jump size is log-normal
        std::poisson_distribution<int> poisson_dist(config_.jump_intensity * dt);
        int num_jumps = poisson_dist(rng_);

        double jump_term = 0.0;
        for (int i = 0; i < num_jumps; ++i) {
            double jump_z = normal_dist(rng_);
            jump_term += config_.jump_mean + config_.jump_std * jump_z;
        }

        current_price_ *= std::exp(drift_term + diffusion_term + jump_term);
        last_update_ = t;
    }

    [[nodiscard]] Price true_price() const override {
        return Price{static_cast<std::uint64_t>(std::round(current_price_))};
    }

    [[nodiscard]] Timestamp last_update() const override { return last_update_; }

    [[nodiscard]] const JumpDiffusionConfig& config() const { return config_; }

private:
    JumpDiffusionConfig config_;
    double current_price_;
    Timestamp last_update_;
    std::mt19937_64 rng_;
};

/**
 * Dummy fair price source for testing.
 * Allows manual price setting via set_price().
 */
class DummyFairPriceSource : public IFairPriceSource {
public:
    explicit DummyFairPriceSource(Price initial_price = Price{100})
        : current_price_(initial_price), last_update_(Timestamp{0}) {}

    void advance_to(Timestamp t) override { last_update_ = t; }

    [[nodiscard]] Price true_price() const override { return current_price_; }

    [[nodiscard]] Timestamp last_update() const override { return last_update_; }

    void set_price(Price price) { current_price_ = price; }

private:
    Price current_price_;
    Timestamp last_update_;
};
