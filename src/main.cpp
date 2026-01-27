#include "agents/noise_trader.hpp"
#include "simulation/simulation_engine.hpp"

#include <iostream>

int main() {
    SimulationEngine sim(Timestamp{10}); // 10 unit latency

    sim.add_instrument(InstrumentID{1});

    NoiseTraderConfig config{.instrument = InstrumentID{1},
                             .fair_value = Price{1000},
                             .spread = Price{50},
                             .min_quantity = Quantity{10},
                             .max_quantity = Quantity{100},
                             .min_interval = Timestamp{50},
                             .max_interval = Timestamp{200}};

    // Add several noise traders with different seeds
    for (std::uint64_t i = 1; i <= 5; ++i) {
        sim.add_agent<NoiseTrader>(ClientID{i}, config, /*seed=*/i * 100);
        sim.scheduler().schedule(
            AgentWakeup{.timestamp = Timestamp{i * 10}, .agent_id = ClientID{i}});
    }

    // Seed the order book with some initial liquidity
    sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                            .agent_id = ClientID{100},
                                            .instrument_id = InstrumentID{1},
                                            .quantity = Quantity{500},
                                            .price = Price{990},
                                            .side = OrderSide::BUY,
                                            .type = OrderType::LIMIT});

    sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                            .agent_id = ClientID{100},
                                            .instrument_id = InstrumentID{1},
                                            .quantity = Quantity{500},
                                            .price = Price{980},
                                            .side = OrderSide::BUY,
                                            .type = OrderType::LIMIT});

    sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                            .agent_id = ClientID{101},
                                            .instrument_id = InstrumentID{1},
                                            .quantity = Quantity{500},
                                            .price = Price{1010},
                                            .side = OrderSide::SELL,
                                            .type = OrderType::LIMIT});

    sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                            .agent_id = ClientID{101},
                                            .instrument_id = InstrumentID{1},
                                            .quantity = Quantity{500},
                                            .price = Price{1020},
                                            .side = OrderSide::SELL,
                                            .type = OrderType::LIMIT});

    std::cout << "Initial order book:\n";
    sim.run_until(Timestamp{1});
    sim.print_book();

    std::cout << "\nRunning simulation...\n";
    sim.run_until(Timestamp{10000});
    std::cout << "Simulation complete. Time: " << sim.now() << "\n\n";

    std::cout << "Final order book:\n";
    sim.print_book();

    return 0;
}
