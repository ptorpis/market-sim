#pragma once

#include "config/configs.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace config_loader_detail {

/// Validates that a JSON value is a non-negative integer suitable for uint64_t
inline std::uint64_t get_uint64(const nlohmann::json& j, const std::string& key) {
    const auto& val = j.at(key);

    if (val.is_number_integer()) {
        auto signed_val = val.get<std::int64_t>();
        if (signed_val < 0) {
            throw std::runtime_error("Value for '" + key + "' must be non-negative, got: " +
                                     std::to_string(signed_val));
        }
        return static_cast<std::uint64_t>(signed_val);
    }

    if (val.is_number_unsigned()) {
        return val.get<std::uint64_t>();
    }

    if (val.is_number_float()) {
        auto float_val = val.get<double>();
        if (float_val < 0.0) {
            throw std::runtime_error("Value for '" + key + "' must be non-negative");
        }
        if (float_val > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            throw std::runtime_error("Value for '" + key + "' exceeds maximum allowed value");
        }
        if (std::isnan(float_val) || std::isinf(float_val)) {
            throw std::runtime_error("Value for '" + key + "' must be a finite number");
        }
        return static_cast<std::uint64_t>(float_val);
    }

    throw std::runtime_error("Value for '" + key + "' must be a number");
}

/// Validates that a JSON value is a non-negative integer suitable for uint32_t
inline std::uint32_t get_uint32(const nlohmann::json& j, const std::string& key) {
    auto val = get_uint64(j, key);
    if (val > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Value for '" + key + "' exceeds maximum allowed value");
    }
    return static_cast<std::uint32_t>(val);
}

} // namespace config_loader_detail

inline void from_json(const nlohmann::json& j, FairPriceConfig& c) {
    using namespace config_loader_detail;
    c.initial_price = Price{get_uint64(j, "initial_price")};
    c.drift = j.at("drift").get<double>();
    c.volatility = j.at("volatility").get<double>();
    c.tick_size = Timestamp{get_uint64(j, "tick_size")};
}

inline void from_json(const nlohmann::json& j, JumpDiffusionConfig& c) {
    using namespace config_loader_detail;
    c.initial_price = Price{get_uint64(j, "initial_price")};
    c.drift = j.at("drift").get<double>();
    c.volatility = j.at("volatility").get<double>();
    c.tick_size = Timestamp{get_uint64(j, "tick_size")};
    c.jump_intensity = j.at("jump_intensity").get<double>();
    c.jump_mean = j.at("jump_mean").get<double>();
    c.jump_std = j.at("jump_std").get<double>();
}

inline FairPriceModelConfig parse_fair_price_config(const nlohmann::json& j) {
    std::string model = j.value("model", "gbm");
    if (model == "jump_diffusion") {
        return j.get<JumpDiffusionConfig>();
    }
    // GBM model: reject if jump diffusion params are present
    if (j.contains("jump_intensity") || j.contains("jump_mean") || j.contains("jump_std")) {
        throw std::runtime_error(
            "GBM model cannot have jump diffusion parameters (jump_intensity, jump_mean, jump_std). "
            "Use model='jump_diffusion' instead.");
    }
    return j.get<FairPriceConfig>();
}

inline void from_json(const nlohmann::json& j, NoiseTraderConfig& c) {
    using namespace config_loader_detail;
    c.instrument = InstrumentID{get_uint32(j, "instrument")};
    c.observation_noise = j.at("observation_noise").get<double>();
    c.spread = Price{get_uint64(j, "spread")};
    c.min_quantity = Quantity{get_uint64(j, "min_quantity")};
    c.max_quantity = Quantity{get_uint64(j, "max_quantity")};
    c.min_interval = Timestamp{get_uint64(j, "min_interval")};
    c.max_interval = Timestamp{get_uint64(j, "max_interval")};
    c.adverse_fill_threshold = Price{get_uint64(j, "adverse_fill_threshold")};
    c.stale_order_threshold = Price{get_uint64(j, "stale_order_threshold")};
}

inline void from_json(const nlohmann::json& j, NoiseTraderGroupConfig& c) {
    using namespace config_loader_detail;
    c.count = get_uint64(j, "count");
    c.start_client_id = ClientID{get_uint64(j, "start_client_id")};
    c.base_seed = get_uint64(j, "base_seed");
    c.initial_wakeup_start = Timestamp{get_uint64(j, "initial_wakeup_start")};
    c.initial_wakeup_step = Timestamp{get_uint64(j, "initial_wakeup_step")};
    c.config = j.at("config").get<NoiseTraderConfig>();
}

inline void from_json(const nlohmann::json& j, MarketMakerConfig& c) {
    using namespace config_loader_detail;
    c.instrument = InstrumentID{get_uint32(j, "instrument")};
    c.observation_noise = j.at("observation_noise").get<double>();
    c.half_spread = Price{get_uint64(j, "half_spread")};
    c.quote_size = Quantity{get_uint64(j, "quote_size")};
    c.update_interval = Timestamp{get_uint64(j, "update_interval")};
    c.inventory_skew_factor = j.at("inventory_skew_factor").get<double>();
    c.max_position = Quantity{get_uint64(j, "max_position")};
}

inline void from_json(const nlohmann::json& j, InformedTraderConfig& c) {
    using namespace config_loader_detail;
    c.instrument = InstrumentID{get_uint32(j, "instrument")};
    c.min_quantity = Quantity{get_uint64(j, "min_quantity")};
    c.max_quantity = Quantity{get_uint64(j, "max_quantity")};
    c.min_interval = Timestamp{get_uint64(j, "min_interval")};
    c.max_interval = Timestamp{get_uint64(j, "max_interval")};
    c.min_edge = Price{get_uint64(j, "min_edge")};
    c.observation_noise = j.at("observation_noise").get<double>();
    c.adverse_fill_threshold = Price{get_uint64(j, "adverse_fill_threshold")};
    c.stale_order_threshold = Price{get_uint64(j, "stale_order_threshold")};
}

inline void from_json(const nlohmann::json& j, AgentConfig& c) {
    using namespace config_loader_detail;
    c.id = ClientID{get_uint64(j, "client_id")};
    c.type = j.at("type").get<std::string>();
    c.seed = get_uint64(j, "seed");
    c.initial_wakeup = Timestamp{get_uint64(j, "initial_wakeup")};
    if (j.contains("latency")) {
        c.latency = Timestamp{get_uint64(j, "latency")};
    }

    const auto& config = j.at("config");
    if (c.type == "NoiseTrader") {
        c.noise_trader = config.get<NoiseTraderConfig>();
    } else if (c.type == "MarketMaker") {
        c.market_maker = config.get<MarketMakerConfig>();
    } else if (c.type == "InformedTrader") {
        c.informed_trader = config.get<InformedTraderConfig>();
    } else {
        throw std::runtime_error("Unknown agent type: " + c.type);
    }
}

inline void from_json(const nlohmann::json& j, InitialOrder& o) {
    using namespace config_loader_detail;
    o.instrument = InstrumentID{get_uint32(j, "instrument")};
    std::string side_str = j.at("side").get<std::string>();
    o.side = (side_str == "BUY") ? OrderSide::BUY : OrderSide::SELL;
    o.price = Price{get_uint64(j, "price")};
    o.quantity = Quantity{get_uint64(j, "quantity")};
}

inline void from_json(const nlohmann::json& j, SimulationConfig& c) {
    using namespace config_loader_detail;

    if (!j.is_object()) {
        throw std::runtime_error("SimulationConfig must be a JSON object");
    }

    if (j.contains("simulation")) {
        const auto& sim = j.at("simulation");
        if (!sim.is_object()) {
            throw std::runtime_error("'simulation' must be a JSON object");
        }
        if (sim.contains("latency")) {
            c.latency = Timestamp{get_uint64(sim, "latency")};
        }
        if (sim.contains("duration")) {
            c.duration = Timestamp{get_uint64(sim, "duration")};
        }
        if (sim.contains("output_dir")) {
            c.output_dir = sim.at("output_dir").get<std::string>();
        }
        if (sim.contains("pnl_snapshot_interval")) {
            c.pnl_snapshot_interval = Timestamp{get_uint64(sim, "pnl_snapshot_interval")};
        }
    }

    if (j.contains("instruments")) {
        const auto& instruments = j.at("instruments");
        if (!instruments.is_array()) {
            throw std::runtime_error("'instruments' must be a JSON array");
        }
        for (std::size_t i = 0; i < instruments.size(); ++i) {
            const auto& id = instruments[i];
            if (!id.is_number()) {
                throw std::runtime_error("instruments[" + std::to_string(i) +
                                         "] must be a number");
            }
            if (id.is_number_integer() && id.get<std::int64_t>() < 0) {
                throw std::runtime_error("instruments[" + std::to_string(i) +
                                         "] must be non-negative");
            }
            c.instruments.push_back(InstrumentID{id.get<std::uint32_t>()});
        }
    }

    if (j.contains("fair_price")) {
        const auto& fp = j.at("fair_price");
        c.fair_price = parse_fair_price_config(fp);
        if (fp.contains("seed")) {
            c.fair_price_seed = get_uint64(fp, "seed");
        }
    }

    if (j.contains("noise_traders")) {
        c.noise_traders = j.at("noise_traders").get<NoiseTraderGroupConfig>();
    }

    if (j.contains("agents")) {
        const auto& agents = j.at("agents");
        if (!agents.is_array()) {
            throw std::runtime_error("'agents' must be a JSON array");
        }
        for (const auto& agent : agents) {
            c.agents.push_back(agent.get<AgentConfig>());
        }
    }

    if (j.contains("initial_orders")) {
        const auto& orders = j.at("initial_orders");
        if (!orders.is_array()) {
            throw std::runtime_error("'initial_orders' must be a JSON array");
        }
        for (const auto& order : orders) {
            c.initial_orders.push_back(order.get<InitialOrder>());
        }
    }
}

inline SimulationConfig load_config(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + path.string());
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse config file: " + std::string(e.what()));
    }

    return j.get<SimulationConfig>();
}
