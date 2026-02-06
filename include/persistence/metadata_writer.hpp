#pragma once

#include "config/configs.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

// JSON serialization for config types
inline nlohmann::json to_json(const NoiseTraderConfig& c) {
    return {{"instrument", c.instrument.value()},
            {"observation_noise", c.observation_noise},
            {"spread", c.spread.value()},
            {"min_quantity", c.min_quantity.value()},
            {"max_quantity", c.max_quantity.value()},
            {"min_interval", c.min_interval.value()},
            {"max_interval", c.max_interval.value()},
            {"adverse_fill_threshold", c.adverse_fill_threshold.value()},
            {"stale_order_threshold", c.stale_order_threshold.value()}};
}

inline nlohmann::json to_json(const MarketMakerConfig& c) {
    return {{"instrument", c.instrument.value()},
            {"observation_noise", c.observation_noise},
            {"half_spread", c.half_spread.value()},
            {"quote_size", c.quote_size.value()},
            {"update_interval", c.update_interval.value()},
            {"inventory_skew_factor", c.inventory_skew_factor},
            {"max_position", c.max_position.value()}};
}

inline nlohmann::json to_json(const InformedTraderConfig& c) {
    return {{"instrument", c.instrument.value()},
            {"min_quantity", c.min_quantity.value()},
            {"max_quantity", c.max_quantity.value()},
            {"min_interval", c.min_interval.value()},
            {"max_interval", c.max_interval.value()},
            {"min_edge", c.min_edge.value()},
            {"observation_noise", c.observation_noise},
            {"adverse_fill_threshold", c.adverse_fill_threshold.value()},
            {"stale_order_threshold", c.stale_order_threshold.value()}};
}

inline nlohmann::json to_json(const FairPriceConfig& c) {
    return {{"model", "gbm"},
            {"initial_price", c.initial_price.value()},
            {"drift", c.drift},
            {"volatility", c.volatility},
            {"tick_size", c.tick_size.value()}};
}

inline nlohmann::json to_json(const JumpDiffusionConfig& c) {
    return {{"model", "jump_diffusion"},
            {"initial_price", c.initial_price.value()},
            {"drift", c.drift},
            {"volatility", c.volatility},
            {"tick_size", c.tick_size.value()},
            {"jump_intensity", c.jump_intensity},
            {"jump_mean", c.jump_mean},
            {"jump_std", c.jump_std}};
}

inline nlohmann::json to_json(const FairPriceModelConfig& c) {
    return std::visit([](const auto& config) { return to_json(config); }, c);
}

class MetadataWriter {
public:
    void set_simulation_config(Timestamp latency) {
        simulation_["latency"] = latency.value();
    }

    void add_instrument(InstrumentID id) { instruments_.push_back(id.value()); }

    void set_fair_price(const FairPriceModelConfig& config, std::uint64_t seed) {
        fair_price_ = to_json(config);
        fair_price_["seed"] = seed;
    }

    void add_agent(ClientID id, const std::string& type, const nlohmann::json& config,
                   std::uint64_t seed, Timestamp latency = Timestamp{0}) {
        agents_.push_back({{"client_id", id.value()},
                           {"type", type},
                           {"config", config},
                           {"seed", seed},
                           {"latency", latency.value()}});
    }

    void set_duration(Timestamp duration) { simulation_["duration"] = duration.value(); }

    void write(const std::filesystem::path& output_dir) const {
        nlohmann::json metadata;
        metadata["simulation"] = simulation_;
        metadata["instruments"] = instruments_;
        if (!fair_price_.is_null()) {
            metadata["fair_price"] = fair_price_;
        }
        metadata["agents"] = agents_;

        std::ofstream file(output_dir / "metadata.json");
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open metadata.json for writing");
        }
        file << metadata.dump(2);
    }

private:
    nlohmann::json simulation_;
    std::vector<std::uint32_t> instruments_;
    nlohmann::json fair_price_;
    nlohmann::json agents_ = nlohmann::json::array();
};
