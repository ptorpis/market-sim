#pragma once

#include "exchange/types.hpp"
#include "simulation/events.hpp"
#include "utils/types.hpp"

/**
 * Shared simulation environment interface passed to all agents.
 *
 * Provides agents with access to market state and actions (submit/cancel/modify orders).
 * A single instance (SimulationEngine) is shared across all agents. The context tracks
 * which agent is currently executing, so actions are attributed to the correct participant.
 */
class AgentContext {
public:
    virtual ~AgentContext() = default;

    virtual void submit_order(InstrumentID instrument, Quantity qty, Price price,
                              OrderSide side, OrderType type) = 0;
    virtual void cancel_order(OrderID order_id) = 0;
    virtual void modify_order(OrderID order_id, Quantity new_qty, Price new_price) = 0;

    virtual void schedule_wakeup(Timestamp at) = 0;

    virtual const OrderBook& get_order_book(InstrumentID instrument) const = 0;

    [[nodiscard]] virtual Price fair_price() const = 0;

    [[nodiscard]] virtual Timestamp now() const = 0;
};

/**
 * Base class for individual trading participants in the simulation.
 *
 * Each Agent instance represents a single trader with its own state and strategy.
 * Agents receive callbacks (on_wakeup, on_trade, etc.) with a reference to the shared
 * AgentContext, through which they can observe market state and submit actions.
 */
class Agent {
public:
    explicit Agent(ClientID id) : id_(id) {}
    virtual ~Agent() = default;

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent(Agent&&) = default;
    Agent& operator=(Agent&&) = default;

    [[nodiscard]] ClientID id() const { return id_; }

    virtual void on_wakeup(AgentContext& ctx) = 0;

    virtual void on_trade([[maybe_unused]] AgentContext& ctx,
                          [[maybe_unused]] const Trade& trade) {}

    virtual void on_order_accepted([[maybe_unused]] AgentContext& ctx,
                                   [[maybe_unused]] const OrderAccepted& event) {}
    virtual void on_order_rejected([[maybe_unused]] AgentContext& ctx,
                                   [[maybe_unused]] const OrderRejected& event) {}
    virtual void on_order_cancelled([[maybe_unused]] AgentContext& ctx,
                                    [[maybe_unused]] const OrderCancelled& event) {}
    virtual void on_order_modified([[maybe_unused]] AgentContext& ctx,
                                   [[maybe_unused]] const OrderModified& event) {}

private:
    ClientID id_;
};
