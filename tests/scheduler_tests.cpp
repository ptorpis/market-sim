#include <gtest/gtest.h>
#include <tuple>

#include "simulation/scheduler.hpp"

class SchedulerTest : public ::testing::Test {
protected:
    void SetUp() override { scheduler = std::make_unique<Scheduler>(); }

    void TearDown() override { scheduler.reset(); }

    std::unique_ptr<Scheduler> scheduler;

    OrderSubmitted make_order_event(Timestamp ts, ClientID agent) {
        return OrderSubmitted{.timestamp = ts,
                              .agent_id = agent,
                              .instrument_id = InstrumentID{1},
                              .quantity = Quantity{100},
                              .price = Price{1000},
                              .side = OrderSide::BUY,
                              .type = OrderType::LIMIT};
    }

    AgentWakeup make_wakeup_event(Timestamp ts, ClientID agent) {
        return AgentWakeup{.timestamp = ts, .agent_id = agent};
    }
};

// =============================================================================
// Basic Operations
// =============================================================================

TEST_F(SchedulerTest, EmptyOnConstruction) {
    EXPECT_TRUE(scheduler->empty());
    EXPECT_EQ(scheduler->size(), 0);
}

TEST_F(SchedulerTest, ScheduleSingleEvent) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));

    EXPECT_FALSE(scheduler->empty());
    EXPECT_EQ(scheduler->size(), 1);
}

TEST_F(SchedulerTest, PopSingleEvent) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));

    Event event = scheduler->pop();

    EXPECT_TRUE(scheduler->empty());
    EXPECT_EQ(get_timestamp(event), Timestamp{100});
}

TEST_F(SchedulerTest, PeekDoesNotRemove) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));

    const Event& peeked = scheduler->peek();
    EXPECT_EQ(get_timestamp(peeked), Timestamp{100});
    EXPECT_EQ(scheduler->size(), 1);

    const Event& peeked_again = scheduler->peek();
    EXPECT_EQ(get_timestamp(peeked_again), Timestamp{100});
    EXPECT_EQ(scheduler->size(), 1);
}

// =============================================================================
// Timestamp Ordering
// =============================================================================

TEST_F(SchedulerTest, EventsOrderedByTimestamp) {
    scheduler->schedule(make_order_event(Timestamp{300}, ClientID{1}));
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{2}));
    scheduler->schedule(make_order_event(Timestamp{200}, ClientID{3}));

    EXPECT_EQ(get_timestamp(scheduler->pop()), Timestamp{100});
    EXPECT_EQ(get_timestamp(scheduler->pop()), Timestamp{200});
    EXPECT_EQ(get_timestamp(scheduler->pop()), Timestamp{300});
}

TEST_F(SchedulerTest, EarlierTimestampAlwaysFirst) {
    for (int i = 10; i >= 1; --i) {
        scheduler->schedule(
            make_order_event(Timestamp{static_cast<uint64_t>(i * 100)}, ClientID{1}));
    }

    for (int i = 1; i <= 10; ++i) {
        EXPECT_EQ(get_timestamp(scheduler->pop()),
                  Timestamp{static_cast<uint64_t>(i * 100)});
    }
}

// =============================================================================
// Sequence Ordering (FIFO for same timestamp)
// =============================================================================

TEST_F(SchedulerTest, SameTimestampOrderedBySequence) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{2}));
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{3}));

    auto event1 = scheduler->pop();
    auto event2 = scheduler->pop();
    auto event3 = scheduler->pop();

    EXPECT_EQ(std::get<OrderSubmitted>(event1).agent_id, ClientID{1});
    EXPECT_EQ(std::get<OrderSubmitted>(event2).agent_id, ClientID{2});
    EXPECT_EQ(std::get<OrderSubmitted>(event3).agent_id, ClientID{3});
}

TEST_F(SchedulerTest, FIFOWithinSameTimestamp) {
    for (int i = 1; i <= 100; ++i) {
        scheduler->schedule(
            make_order_event(Timestamp{500}, ClientID{static_cast<uint64_t>(i)}));
    }

    for (int i = 1; i <= 100; ++i) {
        auto event = scheduler->pop();
        EXPECT_EQ(std::get<OrderSubmitted>(event).agent_id,
                  ClientID{static_cast<uint64_t>(i)});
    }
}

TEST_F(SchedulerTest, MixedTimestampsAndSequences) {
    scheduler->schedule(make_order_event(Timestamp{200}, ClientID{1}));
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{2}));
    scheduler->schedule(make_order_event(Timestamp{200}, ClientID{3}));
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{4}));

    EXPECT_EQ(std::get<OrderSubmitted>(scheduler->pop()).agent_id, ClientID{2});
    EXPECT_EQ(std::get<OrderSubmitted>(scheduler->pop()).agent_id, ClientID{4});
    EXPECT_EQ(std::get<OrderSubmitted>(scheduler->pop()).agent_id, ClientID{1});
    EXPECT_EQ(std::get<OrderSubmitted>(scheduler->pop()).agent_id, ClientID{3});
}

// =============================================================================
// Time Tracking
// =============================================================================

TEST_F(SchedulerTest, NowStartsAtZero) {
    EXPECT_EQ(scheduler->now(), Timestamp{0});
}

TEST_F(SchedulerTest, NowUpdatesOnPop) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));
    scheduler->schedule(make_order_event(Timestamp{250}, ClientID{2}));

    EXPECT_EQ(scheduler->now(), Timestamp{0});

    scheduler->pop();
    EXPECT_EQ(scheduler->now(), Timestamp{100});

    scheduler->pop();
    EXPECT_EQ(scheduler->now(), Timestamp{250});
}

TEST_F(SchedulerTest, NowDoesNotChangeOnPeek) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));

    EXPECT_EQ(scheduler->now(), Timestamp{0});
    std::ignore = scheduler->peek();
    EXPECT_EQ(scheduler->now(), Timestamp{0});
}

TEST_F(SchedulerTest, NowTracksEventTimestamps) {
    scheduler->schedule(make_order_event(Timestamp{50}, ClientID{1}));
    scheduler->schedule(make_order_event(Timestamp{150}, ClientID{2}));
    scheduler->schedule(make_order_event(Timestamp{75}, ClientID{3}));

    scheduler->pop();
    EXPECT_EQ(scheduler->now(), Timestamp{50});

    scheduler->pop();
    EXPECT_EQ(scheduler->now(), Timestamp{75});

    scheduler->pop();
    EXPECT_EQ(scheduler->now(), Timestamp{150});
}

// =============================================================================
// Clear
// =============================================================================

TEST_F(SchedulerTest, ClearEmptiesQueue) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));
    scheduler->schedule(make_order_event(Timestamp{200}, ClientID{2}));

    scheduler->clear();

    EXPECT_TRUE(scheduler->empty());
    EXPECT_EQ(scheduler->size(), 0);
}

TEST_F(SchedulerTest, ClearResetsTime) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));
    scheduler->pop();

    EXPECT_EQ(scheduler->now(), Timestamp{100});

    scheduler->clear();

    EXPECT_EQ(scheduler->now(), Timestamp{0});
}

TEST_F(SchedulerTest, ClearResetsSequence) {
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{1}));
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{2}));
    scheduler->clear();

    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{10}));
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{20}));

    EXPECT_EQ(std::get<OrderSubmitted>(scheduler->pop()).agent_id, ClientID{10});
    EXPECT_EQ(std::get<OrderSubmitted>(scheduler->pop()).agent_id, ClientID{20});
}

// =============================================================================
// Different Event Types
// =============================================================================

TEST_F(SchedulerTest, MixedEventTypes) {
    scheduler->schedule(make_wakeup_event(Timestamp{150}, ClientID{1}));
    scheduler->schedule(make_order_event(Timestamp{100}, ClientID{2}));
    scheduler->schedule(make_wakeup_event(Timestamp{50}, ClientID{3}));

    auto event1 = scheduler->pop();
    auto event2 = scheduler->pop();
    auto event3 = scheduler->pop();

    EXPECT_TRUE(std::holds_alternative<AgentWakeup>(event1));
    EXPECT_TRUE(std::holds_alternative<OrderSubmitted>(event2));
    EXPECT_TRUE(std::holds_alternative<AgentWakeup>(event3));

    EXPECT_EQ(std::get<AgentWakeup>(event1).agent_id, ClientID{3});
    EXPECT_EQ(std::get<OrderSubmitted>(event2).agent_id, ClientID{2});
    EXPECT_EQ(std::get<AgentWakeup>(event3).agent_id, ClientID{1});
}
