#pragma once

#include "config/configs.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

inline void from_json(const nlohmann::json& j, FairPriceConfig& c) {
    c.initial_price = Price{j.at("initial_price").get<std::uint64_t>()};
    c.drift = j.at("drift").get<double>();
    c.volatility = j.at("volatility").get<double>();
    c.tick_size = Timestamp{j.at("tick_size").get<std::uint64_t>()};
}

inline void from_json(const nlohmann::json& j, NoiseTraderConfig& c) {
    c.instrument = InstrumentID{j.at("instrument").get<std::uint32_t>()};
    c.observation_noise = j.at("observation_noise").get<double>();
    c.spread = Price{j.at("spread").get<std::uint64_t>()};
    c.min_quantity = Quantity{j.at("min_quantity").get<std::uint64_t>()};
    c.max_quantity = Quantity{j.at("max_quantity").get<std::uint64_t>()};
    c.min_interval = Timestamp{j.at("min_interval").get<std::uint64_t>()};
    c.max_interval = Timestamp{j.at("max_interval").get<std::uint64_t>()};
    c.adverse_fill_threshold =
        Price{j.at("adverse_fill_threshold").get<std::uint64_t>()};
    c.stale_order_threshold = Price{j.at("stale_order_threshold").get<std::uint64_t>()};
}

inline void from_json(const nlohmann::json& j, MarketMakerConfig& c) {
    c.instrument = InstrumentID{j.at("instrument").get<std::uint32_t>()};
    c.observation_noise = j.at("observation_noise").get<double>();
    c.half_spread = Price{j.at("half_spread").get<std::uint64_t>()};
    c.quote_size = Quantity{j.at("quote_size").get<std::uint64_t>()};
    c.update_interval = Timestamp{j.at("update_interval").get<std::uint64_t>()};
    c.inventory_skew_factor = j.at("inventory_skew_factor").get<double>();
    c.max_position = Quantity{j.at("max_position").get<std::uint64_t>()};
}

inline void from_json(const nlohmann::json& j, InformedTraderConfig& c) {
    c.instrument = InstrumentID{j.at("instrument").get<std::uint32_t>()};
    c.min_quantity = Quantity{j.at("min_quantity").get<std::uint64_t>()};
    c.max_quantity = Quantity{j.at("max_quantity").get<std::uint64_t>()};
    c.min_interval = Timestamp{j.at("min_interval").get<std::uint64_t>()};
    c.max_interval = Timestamp{j.at("max_interval").get<std::uint64_t>()};
    c.min_edge = Price{j.at("min_edge").get<std::uint64_t>()};
    c.observation_noise = j.at("observation_noise").get<double>();
    c.adverse_fill_threshold =
        Price{j.at("adverse_fill_threshold").get<std::uint64_t>()};
    c.stale_order_threshold = Price{j.at("stale_order_threshold").get<std::uint64_t>()};
}

inline void from_json(const nlohmann::json& j, AgentConfig& c) {
    c.id = ClientID{j.at("client_id").get<std::uint64_t>()};
    c.type = j.at("type").get<std::string>();
    c.seed = j.at("seed").get<std::uint64_t>();
    c.initial_wakeup = Timestamp{j.at("initial_wakeup").get<std::uint64_t>()};
    if (j.contains("latency")) {
        c.latency = Timestamp{j.at("latency").get<std::uint64_t>()};
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
    o.instrument = InstrumentID{j.at("instrument").get<std::uint32_t>()};
    std::string side_str = j.at("side").get<std::string>();
    o.side = (side_str == "BUY") ? OrderSide::BUY : OrderSide::SELL;
    o.price = Price{j.at("price").get<std::uint64_t>()};
    o.quantity = Quantity{j.at("quantity").get<std::uint64_t>()};
}

inline void from_json(const nlohmann::json& j, SimulationConfig& c) {
    if (j.contains("simulation")) {
        const auto& sim = j.at("simulation");
        if (sim.contains("latency")) {
            c.latency = Timestamp{sim.at("latency").get<std::uint64_t>()};
        }
        if (sim.contains("duration")) {
            c.duration = Timestamp{sim.at("duration").get<std::uint64_t>()};
        }
        if (sim.contains("output_dir")) {
            c.output_dir = sim.at("output_dir").get<std::string>();
        }
        if (sim.contains("pnl_snapshot_interval")) {
            c.pnl_snapshot_interval =
                Timestamp{sim.at("pnl_snapshot_interval").get<std::uint64_t>()};
        }
    }

    if (j.contains("instruments")) {
        for (const auto& id : j.at("instruments")) {
            c.instruments.push_back(InstrumentID{id.get<std::uint32_t>()});
        }
    }

    if (j.contains("fair_price")) {
        const auto& fp = j.at("fair_price");
        c.fair_price = fp.get<FairPriceConfig>();
        if (fp.contains("seed")) {
            c.fair_price_seed = fp.at("seed").get<std::uint64_t>();
        }
    }

    if (j.contains("agents")) {
        for (const auto& agent : j.at("agents")) {
            c.agents.push_back(agent.get<AgentConfig>());
        }
    }

    if (j.contains("initial_orders")) {
        for (const auto& order : j.at("initial_orders")) {
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
