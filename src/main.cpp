#include "agents/informed_trader.hpp"
#include "agents/market_maker.hpp"
#include "agents/noise_trader.hpp"
#include "config/config_loader.hpp"
#include "persistence/metadata_writer.hpp"
#include "simulation/simulation_engine.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>

void run_from_config(const SimulationConfig& config) {
    SimulationEngine sim(config.latency);

    sim.enable_persistence(config.output_dir, config.pnl_snapshot_interval);

    for (const auto& instrument : config.instruments) {
        sim.add_instrument(instrument);
    }

    sim.set_fair_price(config.fair_price, config.fair_price_seed);

    // Expand noise trader group
    if (config.noise_traders) {
        const auto& group = *config.noise_traders;
        for (std::uint64_t i = 0; i < group.count; ++i) {
            ClientID id{group.start_client_id.value() + i};
            std::uint64_t seed = group.base_seed + i;
            Timestamp wakeup{group.initial_wakeup_start.value() +
                             i * group.initial_wakeup_step.value()};

            sim.add_agent<NoiseTrader>(id, group.config, seed);
            sim.set_agent_latency_jitter(id, group.config.latency_jitter, seed);
            if (sim.data_collector()) {
                sim.data_collector()->metadata().add_agent(
                    id, "NoiseTrader", to_json(group.config), seed, Timestamp{0});
            }
            sim.scheduler().schedule(AgentWakeup{.timestamp = wakeup, .agent_id = id});
        }
    }

    for (const auto& agent : config.agents) {
        double jitter = 0.0;
        if (agent.type == "NoiseTrader") {
            sim.add_agent<NoiseTrader>(agent.id, agent.noise_trader, agent.seed);
            jitter = agent.noise_trader.latency_jitter;
            if (sim.data_collector()) {
                sim.data_collector()->metadata().add_agent(agent.id, "NoiseTrader",
                                                           to_json(agent.noise_trader),
                                                           agent.seed, agent.latency);
            }
        } else if (agent.type == "MarketMaker") {
            sim.add_agent<MarketMaker>(agent.id, agent.market_maker, agent.seed);
            jitter = agent.market_maker.latency_jitter;
            if (sim.data_collector()) {
                sim.data_collector()->metadata().add_agent(agent.id, "MarketMaker",
                                                           to_json(agent.market_maker),
                                                           agent.seed, agent.latency);
            }
        } else if (agent.type == "InformedTrader") {
            sim.add_agent<InformedTrader>(agent.id, agent.informed_trader, agent.seed);
            jitter = agent.informed_trader.latency_jitter;
            if (sim.data_collector()) {
                sim.data_collector()->metadata().add_agent(agent.id, "InformedTrader",
                                                           to_json(agent.informed_trader),
                                                           agent.seed, agent.latency);
            }
        }
        sim.set_agent_latency(agent.id, agent.latency);
        sim.set_agent_latency_jitter(agent.id, jitter, agent.seed);
        sim.scheduler().schedule(
            AgentWakeup{.timestamp = agent.initial_wakeup, .agent_id = agent.id});
    }

    for (const auto& order : config.initial_orders) {
        sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                                .agent_id = ClientID{0},
                                                .instrument_id = order.instrument,
                                                .quantity = order.quantity,
                                                .price = order.price,
                                                .side = order.side,
                                                .type = OrderType::LIMIT});
    }

    std::cout << "Initial order book:\n";
    sim.run_until(Timestamp{1});
    sim.print_book();

    std::cout << "\nRunning simulation...\n";
    sim.run_until(config.duration);
    std::cout << "Simulation complete. Time: " << sim.now() << "\n\n";

    std::cout << "Final order book:\n";
    sim.print_book();

    Price mark_price = sim.fair_price();
    std::cout << "\nMark price (fair value): " << mark_price << "\n\n";
    sim.print_pnl(mark_price);

    sim.finalize_persistence();
    std::cout << "\nPersistence data written to " << config.output_dir.string() << "/\n";
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --config <path>  Load simulation configuration from JSON file\n";
    std::cout << "  --output <path>  Override output directory (default: from config)\n";
    std::cout << "  --help           Show this help message\n";
    std::cout << "\nIf no config file is specified, tries config.json then "
                 "config_template.json.\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;
    std::string output_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_path = argv[++i];
            } else {
                std::cerr << "Error: --config requires a path argument\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--output") == 0 ||
                   std::strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                std::cerr << "Error: --output requires a path argument\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--help") == 0 ||
                   std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    try {
        SimulationConfig config;
        if (!config_path.empty()) {
            std::cout << "Loading config from: " << config_path << "\n";
            config = load_config(config_path);
        } else if (std::filesystem::exists("config.json")) {
            std::cout << "Loading config from: config.json\n";
            config = load_config("config.json");
        } else if (std::filesystem::exists("config_template.json")) {
            std::cout << "Loading config from: config_template.json\n";
            config = load_config("config_template.json");
        } else {
            std::cerr << "Error: No config file found.\n";
            std::cerr << "Please provide config.json, config_template.json, or use "
                         "--config <path>\n";
            return 1;
        }

        if (!output_path.empty()) {
            config.output_dir = output_path;
        }
        std::cout << "Output directory: " << config.output_dir.string() << "\n\n";

        run_from_config(config);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
