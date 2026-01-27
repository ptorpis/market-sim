#include "agents/informed_trader.hpp"
#include "agents/market_maker.hpp"
#include "agents/noise_trader.hpp"
#include "simulation/simulation_engine.hpp"

#include <iostream>

int main() {
    SimulationEngine sim(Timestamp{10});

    sim.add_instrument(InstrumentID{1});

    sim.set_fair_price(FairPriceConfig{.initial_price = Price{1000},
                                       .drift = 0.0,
                                       .volatility = 0.1,
                                       .tick_size = Timestamp{1000}},
                       /*seed*/ 42);

    // Noise traders provide random liquidity
    NoiseTraderConfig noise_config{.instrument = InstrumentID{1},
                                   .fair_value = Price{1000},
                                   .spread = Price{5},
                                   .min_quantity = Quantity{10},
                                   .max_quantity = Quantity{100},
                                   .min_interval = Timestamp{50},
                                   .max_interval = Timestamp{200}};

    for (std::uint64_t i = 1; i <= 5; ++i) {
        sim.add_agent<NoiseTrader>(ClientID{i}, noise_config, /*seed=*/i * 100);
        sim.scheduler().schedule(
            AgentWakeup{.timestamp = Timestamp{i * 10}, .agent_id = ClientID{i}});
    }

    // Market maker quotes around midpoint with inventory skew
    MarketMakerConfig mm_config{.instrument = InstrumentID{1},
                                .half_spread = Price{5},
                                .quote_size = Quantity{50},
                                .update_interval = Timestamp{100},
                                .inventory_skew_factor = 0.5,
                                .max_position = Quantity{500}};

    sim.add_agent<MarketMaker>(ClientID{10}, mm_config, /*seed=*/999);
    sim.scheduler().schedule(
        AgentWakeup{.timestamp = Timestamp{5}, .agent_id = ClientID{10}});

    // Informed trader observes fair price and trades on edge
    InformedTraderConfig it_config{.instrument = InstrumentID{1},
                                   .min_quantity = Quantity{20},
                                   .max_quantity = Quantity{80},
                                   .min_interval = Timestamp{100},
                                   .max_interval = Timestamp{500},
                                   .min_edge = Price{3},
                                   .observation_noise = 5.0};

    sim.add_agent<InformedTrader>(ClientID{20}, it_config, /*seed=*/777);
    sim.scheduler().schedule(
        AgentWakeup{.timestamp = Timestamp{50}, .agent_id = ClientID{20}});

    // Seed order book with initial liquidity
    sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                            .agent_id = ClientID{100},
                                            .instrument_id = InstrumentID{1},
                                            .quantity = Quantity{500},
                                            .price = Price{995},
                                            .side = OrderSide::BUY,
                                            .type = OrderType::LIMIT});

    sim.scheduler().schedule(OrderSubmitted{.timestamp = Timestamp{0},
                                            .agent_id = ClientID{100},
                                            .instrument_id = InstrumentID{1},
                                            .quantity = Quantity{500},
                                            .price = Price{1005},
                                            .side = OrderSide::SELL,
                                            .type = OrderType::LIMIT});

    std::cout << "Initial order book:\n";
    sim.run_until(Timestamp{1});
    sim.print_book();

    std::cout << "\nRunning simulation...\n";
    sim.run_until(Timestamp{1000000});
    std::cout << "Simulation complete. Time: " << sim.now() << "\n\n";

    std::cout << "Final order book:\n";
    sim.print_book();

    Price mark_price = sim.fair_price();
    std::cout << "\nMark price (fair value): " << mark_price << "\n\n";
    sim.print_pnl(mark_price);

    return 0;
}
