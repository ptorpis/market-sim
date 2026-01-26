#include "time/simulation_timer.hpp"
#include "utils/types.hpp"
#include <format>
#include <iostream>
#include <print>
#include <unordered_map>

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
    Quantity qty = Quantity{200};

    std::puts(std::format("Price={}", price).c_str());

    return 0;
}
