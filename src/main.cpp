#include "time/simulation_timer.hpp"
#include "utils/types.hpp"
#include <format>
#include <iostream>
#include <print>
#include <unordered_map>

#include "exchange/matching_engine.hpp"
#include "exchange/types.hpp"

int main() {
    Timestamp timestamp{100000};
    std::cout << timestamp << std::endl;
    SimulationTimer timer{13};

    for (auto i{0uz}; i < 10; ++i) {
        timer.tick();
        std::cout << timer.now() << std::endl;
    }

    std::println("{}", timestamp);

    std::unordered_map<Timestamp, int, strong_hash<Timestamp>>{};

    Price price = Price{100};

    std::puts(std::format("Price={}", price).c_str());

    std::cout << static_cast<std::uint64_t>(timestamp) << std::endl;

    OrderRequest req = OrderRequest{.client_id = ClientID{1},
                                    .quantity = Quantity{100},
                                    .price = Price{1000},
                                    .instrumentID = InstrumentID{1},
                                    .side = OrderSide::BUY,
                                    .type = OrderType::LIMIT,
                                    .time_in_force = TimeInForce::GOOD_TILL_CANCELLED,
                                    .good_till = Timestamp{0}};

    OrderRequest sell_req =
        OrderRequest{.client_id = ClientID{1},
                     .quantity = Quantity{100},
                     .price = Price{999},
                     .instrumentID = InstrumentID{1},
                     .side = OrderSide::SELL,
                     .type = OrderType::LIMIT,
                     .time_in_force = TimeInForce::GOOD_TILL_CANCELLED,
                     .good_till = Timestamp{0}};

    MatchingEngine engine{InstrumentID{1}};

    auto res = engine.process_order(req);
    [[maybe_unused]] auto res2 = engine.process_order(sell_req);

    std::cout << "remaining:" << res.remaining_quantity << std::endl;

    engine.print_order_book(5);

    return 0;
}
