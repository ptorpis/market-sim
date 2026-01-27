#pragma once

#include "simulation/events.hpp"

#include <queue>
#include <vector>

struct ScheduledEvent {
    Event event;
    std::uint64_t sequence;
};

struct ScheduledEventComparator {
    bool operator()(const ScheduledEvent& a, const ScheduledEvent& b) const {
        Timestamp ta = get_timestamp(a.event);
        Timestamp tb = get_timestamp(b.event);

        if (ta != tb) {
            return ta > tb;
        }
        return a.sequence > b.sequence;
    }
};

class Scheduler {
public:
    void schedule(Event event) {
        queue_.push(
            ScheduledEvent{.event = std::move(event), .sequence = next_sequence_++});
    }

    [[nodiscard]] bool empty() const { return queue_.empty(); }

    [[nodiscard]] std::size_t size() const { return queue_.size(); }

    [[nodiscard]] Timestamp now() const { return current_time_; }

    [[nodiscard]] const Event& peek() const { return queue_.top().event; }

    Event pop() {
        Event event = queue_.top().event;
        current_time_ = get_timestamp(event);
        queue_.pop();
        return event;
    }

    void clear() {
        queue_ = {};
        next_sequence_ = 0;
        current_time_ = Timestamp{0};
    }

private:
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>,
                        ScheduledEventComparator>
        queue_;
    std::uint64_t next_sequence_ = 0;
    Timestamp current_time_{0};
};
