#pragma once

#include "exchange/matching_engine.hpp"
#include "simulation/agent.hpp"
#include "simulation/scheduler.hpp"

#include <memory>
#include <print>
#include <unordered_map>

class SimulationEngine : public AgentContext {
public:
    explicit SimulationEngine(Timestamp latency = Timestamp{0}) : latency_(latency) {}

    // Setup
    void add_instrument(InstrumentID id) {
        engines_.emplace(id, std::make_unique<MatchingEngine>(id));
    }

    template <typename AgentType, typename... Args> AgentType& add_agent(Args&&... args) {
        auto agent = std::make_unique<AgentType>(std::forward<Args>(args)...);
        AgentType& ref = *agent;
        ClientID id = agent->id();
        agents_.emplace(id, std::move(agent));
        return ref;
    }

    // Run simulation
    void run_until(Timestamp end_time) {
        while (!scheduler_.empty() && get_timestamp(scheduler_.peek()) <= end_time) {
            step();
        }
    }

    void step() {
        if (scheduler_.empty()) {
            return;
        }

        Event event = scheduler_.pop();
        dispatch_event(event);
    }

    // AgentContext implementation
    void submit_order(InstrumentID instrument, Quantity qty, Price price, OrderSide side,
                      OrderType type) override {
        scheduler_.schedule(OrderSubmitted{.timestamp = scheduler_.now() + latency_,
                                           .agent_id = current_agent_,
                                           .instrument_id = instrument,
                                           .quantity = qty,
                                           .price = price,
                                           .side = side,
                                           .type = type});
    }

    void cancel_order(OrderID order_id) override {
        scheduler_.schedule(
            CancellationSubmitted{.timestamp = scheduler_.now() + latency_,
                                  .agent_id = current_agent_,
                                  .order_id = order_id});
    }

    void modify_order(OrderID order_id, Quantity new_qty, Price new_price) override {
        scheduler_.schedule(
            ModificationSubmitted{.timestamp = scheduler_.now() + latency_,
                                  .agent_id = current_agent_,
                                  .order_id = order_id,
                                  .new_quantity = new_qty,
                                  .new_price = new_price});
    }

    void schedule_wakeup(Timestamp at) override {
        scheduler_.schedule(AgentWakeup{.timestamp = at, .agent_id = current_agent_});
    }

    const OrderBook& get_order_book(InstrumentID instrument) const override {
        auto it = engines_.find(instrument);
        if (it == engines_.end()) {
            static OrderBook empty;
            return empty;
        }
        return it->second->order_book();
    }

    void print_book(InstrumentID instrument_id = InstrumentID{1}) {
        auto engine = engines_.find(instrument_id);
        if (engine == engines_.end()) {
            std::println("No order book found");
            return;
        }
        engine->second->print_order_book();
    }

    [[nodiscard]] Timestamp now() const override { return scheduler_.now(); }

    // Access for testing
    Scheduler& scheduler() { return scheduler_; }
    const Scheduler& scheduler() const { return scheduler_; }

private:
    Scheduler scheduler_;
    std::unordered_map<InstrumentID, std::unique_ptr<MatchingEngine>,
                       strong_hash<InstrumentID>>
        engines_;
    std::unordered_map<ClientID, std::unique_ptr<Agent>, strong_hash<ClientID>> agents_;
    ClientID current_agent_{0};
    Timestamp latency_;

    void dispatch_event(const Event& event) {
        std::visit(
            [this](const auto& e) {
                handle(e);
            },
            event);
    }

    void handle(const AgentWakeup& event) {
        auto it = agents_.find(event.agent_id);
        if (it == agents_.end()) {
            return;
        }

        current_agent_ = event.agent_id;
        it->second->on_wakeup(*this);
    }

    void handle(const OrderSubmitted& event) {
        auto engine_it = engines_.find(event.instrument_id);
        if (engine_it == engines_.end()) {
            notify_agent(event.agent_id,
                         OrderRejected{.timestamp = scheduler_.now(),
                                       .agent_id = event.agent_id,
                                       .instrument_id = event.instrument_id,
                                       .reason = OrderStatus::CANCELLED});
            return;
        }

        OrderRequest request{.client_id = event.agent_id,
                             .quantity = event.quantity,
                             .price = event.price,
                             .instrument_id = event.instrument_id,
                             .side = event.side,
                             .type = event.type};

        MatchResult result = engine_it->second->process_order(request);

        notify_agent(event.agent_id, OrderAccepted{.timestamp = scheduler_.now(),
                                                   .order_id = result.order_id,
                                                   .agent_id = event.agent_id,
                                                   .instrument_id = event.instrument_id});

        for (const auto& trade_event : result.trade_vec) {
            Trade trade{.timestamp = scheduler_.now(),
                        .trade_id = trade_event.trade_id,
                        .instrument_id = event.instrument_id,
                        .buyer_order_id = trade_event.buyer_order_id,
                        .seller_order_id = trade_event.seller_order_id,
                        .buyer_id = trade_event.buyer_id,
                        .seller_id = trade_event.seller_id,
                        .quantity = trade_event.quantity,
                        .price = trade_event.price};

            notify_trade(trade);
        }
    }

    void handle(const CancellationSubmitted& event) {
        for (auto& [instrument_id, engine] : engines_) {
            auto order = engine->get_order(event.order_id);
            if (!order.has_value()) {
                continue;
            }
            Quantity remaining = order->quantity;
            if (engine->cancel_order(event.agent_id, event.order_id)) {
                notify_agent(event.agent_id,
                             OrderCancelled{.timestamp = scheduler_.now(),
                                            .order_id = event.order_id,
                                            .agent_id = event.agent_id,
                                            .remaining_quantity = remaining});
                return;
            }
        }
    }

    void handle(const ModificationSubmitted& event) {
        for (auto& [instrument_id, engine] : engines_) {
            auto order = engine->get_order(event.order_id);
            if (!order.has_value()) {
                continue;
            }

            Price old_price = order->price;
            Quantity old_quantity = order->quantity;

            ModifyResult result = engine->modify_order(
                event.agent_id, event.order_id, event.new_quantity, event.new_price);

            if (result.status == ModifyStatus::ACCEPTED) {
                notify_agent(event.agent_id,
                             OrderModified{.timestamp = scheduler_.now(),
                                           .old_order_id = event.order_id,
                                           .new_order_id = result.new_order_id,
                                           .agent_id = event.agent_id,
                                           .old_price = old_price,
                                           .new_price = event.new_price,
                                           .old_quantity = old_quantity,
                                           .new_quantity = event.new_quantity});

                if (result.match_result.has_value()) {
                    for (const auto& trade_event : result.match_result->trade_vec) {
                        Trade trade{.timestamp = scheduler_.now(),
                                    .trade_id = trade_event.trade_id,
                                    .instrument_id = instrument_id,
                                    .buyer_order_id = trade_event.buyer_order_id,
                                    .seller_order_id = trade_event.seller_order_id,
                                    .buyer_id = trade_event.buyer_id,
                                    .seller_id = trade_event.seller_id,
                                    .quantity = trade_event.quantity,
                                    .price = trade_event.price};
                        notify_trade(trade);
                    }
                }
            }
            return;
        }
    }

    void handle([[maybe_unused]] const OrderAccepted& event) {}
    void handle([[maybe_unused]] const OrderRejected& event) {}
    void handle([[maybe_unused]] const OrderCancelled& event) {}
    void handle([[maybe_unused]] const OrderModified& event) {}
    void handle([[maybe_unused]] const Trade& event) {}

    void notify_agent(ClientID agent_id, const OrderAccepted& event) {
        auto it = agents_.find(agent_id);
        if (it != agents_.end()) {
            current_agent_ = agent_id;
            it->second->on_order_accepted(*this, event);
        }
    }

    void notify_agent(ClientID agent_id, const OrderRejected& event) {
        auto it = agents_.find(agent_id);
        if (it != agents_.end()) {
            current_agent_ = agent_id;
            it->second->on_order_rejected(*this, event);
        }
    }

    void notify_agent(ClientID agent_id, const OrderCancelled& event) {
        auto it = agents_.find(agent_id);
        if (it != agents_.end()) {
            current_agent_ = agent_id;
            it->second->on_order_cancelled(*this, event);
        }
    }

    void notify_agent(ClientID agent_id, const OrderModified& event) {
        auto it = agents_.find(agent_id);
        if (it != agents_.end()) {
            current_agent_ = agent_id;
            it->second->on_order_modified(*this, event);
        }
    }

    void notify_trade(const Trade& trade) {
        auto buyer_it = agents_.find(trade.buyer_id);
        if (buyer_it != agents_.end()) {
            current_agent_ = trade.buyer_id;
            buyer_it->second->on_trade(*this, trade);
        }

        auto seller_it = agents_.find(trade.seller_id);
        if (seller_it != agents_.end()) {
            current_agent_ = trade.seller_id;
            seller_it->second->on_trade(*this, trade);
        }
    }
};
