#pragma once
#include "utils/types.hpp"
#include <cstdint>

class SimulationTimer {
public:
    SimulationTimer(std::uint16_t dt) : dt_(dt), current_time_({}) {}

    void tick() noexcept;
    [[nodiscard]] Timestamp now() const noexcept;
    void reset() noexcept;

    SimulationTimer(const SimulationTimer&) = delete;
    void operator=(const SimulationTimer&) = delete;
    SimulationTimer(SimulationTimer&&) = delete;
    void operator=(SimulationTimer&&) = delete;
    ~SimulationTimer() = default;

private:
    std::uint16_t dt_{10}; // time delta per tick
    Timestamp current_time_{0};
};