#include "agents/informed_trader.hpp"
#include "agents/market_maker.hpp"
#include "agents/noise_trader.hpp"
#include "config/config_loader.hpp"
#include "persistence/metadata_writer.hpp"
#include "simulation/simulation_engine.hpp"

#include <cstring>
#include <iostream>

void run_from_config(const SimulationConfig& config) {
    SimulationEngine sim(config.latency);

    sim.enable_persistence(config.output_dir, config.pnl_snapshot_interval);

    for (const auto& instrument : config.instruments) {
        sim.add_instrument(instrument);
    }

    sim.set_fair_price(config.fair_price, config.fair_price_seed);

    for (const auto& agent : config.agents) {
        if (agent.type == "NoiseTrader") {
            sim.add_agent<NoiseTrader>(agent.id, agent.noise_trader, agent.seed);
            if (sim.data_collector()) {
                sim.data_collector()->metadata().add_agent(
                    agent.id, "NoiseTrader", to_json(agent.noise_trader), agent.seed);
            }
        } else if (agent.type == "MarketMaker") {
            sim.add_agent<MarketMaker>(agent.id, agent.market_maker, agent.seed);
            if (sim.data_collector()) {
                sim.data_collector()->metadata().add_agent(
                    agent.id, "MarketMaker", to_json(agent.market_maker), agent.seed);
            }
        } else if (agent.type == "InformedTrader") {
            sim.add_agent<InformedTrader>(agent.id, agent.informed_trader, agent.seed);
            if (sim.data_collector()) {
                sim.data_collector()->metadata().add_agent(
                    agent.id, "InformedTrader", to_json(agent.informed_trader), agent.seed);
            }
        }
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

void run_default() {
    SimulationEngine sim(Timestamp{10});

    sim.enable_persistence("./output", Timestamp{100});

    sim.add_instrument(InstrumentID{1});

    sim.set_fair_price(FairPriceConfig{.initial_price = Price{1'000'000},
                                       .drift = 0.0001,
                                       .volatility = 0.005,
                                       .tick_size = Timestamp{1000}},
                       43);

    NoiseTraderConfig noise_config{.instrument = InstrumentID{1},
                                   .fair_value = Price{1'000'000},
                                   .spread = Price{36},
                                   .min_quantity = Quantity{10},
                                   .max_quantity = Quantity{100},
                                   .min_interval = Timestamp{50},
                                   .max_interval = Timestamp{200}};

    for (std::uint64_t i = 1; i <= 5; ++i) {
        sim.add_agent<NoiseTrader>(ClientID{i}, noise_config, i * 100);
        if (sim.data_collector()) {
            sim.data_collector()->metadata().add_agent(ClientID{i}, "NoiseTrader",
                                                       to_json(noise_config), i * 100);
        }
        sim.scheduler().schedule(
            AgentWakeup{.timestamp = Timestamp{i * 10}, .agent_id = ClientID{i}});
    }

    MarketMakerConfig mm_config{.instrument = InstrumentID{1},
                                .half_spread = Price{5},
                                .quote_size = Quantity{50},
                                .update_interval = Timestamp{100},
                                .inventory_skew_factor = 0.5,
                                .max_position = Quantity{500}};

    sim.add_agent<MarketMaker>(ClientID{10}, mm_config, 999);
    if (sim.data_collector()) {
        sim.data_collector()->metadata().add_agent(ClientID{10}, "MarketMaker",
                                                   to_json(mm_config), 999);
    }
    sim.scheduler().schedule(
        AgentWakeup{.timestamp = Timestamp{5}, .agent_id = ClientID{10}});

    InformedTraderConfig it_config{.instrument = InstrumentID{1},
                                   .min_quantity = Quantity{20},
                                   .max_quantity = Quantity{80},
                                   .min_interval = Timestamp{100},
                                   .max_interval = Timestamp{500},
                                   .min_edge = Price{3},
                                   .observation_noise = 5.0};

    sim.add_agent<InformedTrader>(ClientID{20}, it_config, 777);
    if (sim.data_collector()) {
        sim.data_collector()->metadata().add_agent(ClientID{20}, "InformedTrader",
                                                   to_json(it_config), 777);
    }
    sim.scheduler().schedule(
        AgentWakeup{.timestamp = Timestamp{50}, .agent_id = ClientID{20}});

    sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                            .agent_id = ClientID{100},
                                            .instrument_id = InstrumentID{1},
                                            .quantity = Quantity{500},
                                            .price = Price{999'900},
                                            .side = OrderSide::BUY,
                                            .type = OrderType::LIMIT});

    sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                            .agent_id = ClientID{100},
                                            .instrument_id = InstrumentID{1},
                                            .quantity = Quantity{500},
                                            .price = Price{1'000'100},
                                            .side = OrderSide::SELL,
                                            .type = OrderType::LIMIT});

    std::cout << "Initial order book:\n";
    sim.run_until(Timestamp{1});
    sim.print_book();

    std::cout << "\nRunning simulation...\n";
    sim.run_until(Timestamp{1000});
    std::cout << "Simulation complete. Time: " << sim.now() << "\n\n";

    std::cout << "Final order book:\n";
    sim.print_book();

    Price mark_price = sim.fair_price();
    std::cout << "\nMark price (fair value): " << mark_price << "\n\n";
    sim.print_pnl(mark_price);

    sim.finalize_persistence();
    std::cout << "\nPersistence data written to ./output/\n";
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [--config <path>]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --config <path>  Load simulation configuration from JSON file\n";
    std::cout << "  --help           Show this help message\n";
    std::cout << "\nIf no config file is specified, runs with default parameters.\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_path = argv[++i];
            } else {
                std::cerr << "Error: --config requires a path argument\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    try {
        if (!config_path.empty()) {
            std::cout << "Loading config from: " << config_path << "\n\n";
            SimulationConfig config = load_config(config_path);
            run_from_config(config);
        } else {
            run_default();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
