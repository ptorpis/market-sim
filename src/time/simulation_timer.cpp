#include "time/simulation_timer.hpp"

void SimulationTimer::tick() noexcept {
    current_time_ += dt_;
}

void SimulationTimer::reset() noexcept {
    current_time_ = Timestamp{0};
}

[[nodiscard]] Timestamp SimulationTimer::now() const noexcept {
    return current_time_;
}