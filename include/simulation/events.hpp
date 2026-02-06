#pragma once

#include "exchange/types.hpp"
#include "utils/types.hpp"

#include <variant>

struct OrderSubmitted {
    Timestamp timestamp;
    ClientID agent_id;
    InstrumentID instrument_id;
    Quantity quantity;
    Price price;
    OrderSide side;
    OrderType type;
};

struct CancellationSubmitted {
    Timestamp timestamp;
    ClientID agent_id;
    OrderID order_id;
};

struct ModificationSubmitted {
    Timestamp timestamp;
    ClientID agent_id;
    OrderID order_id;
    Quantity new_quantity;
    Price new_price;
};

struct AgentWakeup {
    Timestamp timestamp;
    ClientID agent_id;
};

struct OrderAccepted {
    Timestamp timestamp;
    OrderID order_id;
    ClientID agent_id;
    InstrumentID instrument_id;
};

struct OrderRejected {
    Timestamp timestamp;
    ClientID agent_id;
    InstrumentID instrument_id;
    OrderStatus reason;
};

struct OrderCancelled {
    Timestamp timestamp;
    OrderID order_id;
    ClientID agent_id;
    Quantity remaining_quantity;
};

struct OrderModified {
    Timestamp timestamp;
    OrderID old_order_id;
    OrderID new_order_id;
    ClientID agent_id;
    Price old_price;
    Price new_price;
    Quantity old_quantity;
    Quantity new_quantity;
};

struct Trade {
    Timestamp timestamp;
    TradeID trade_id;
    InstrumentID instrument_id;
    OrderID buyer_order_id;
    OrderID seller_order_id;
    ClientID buyer_id;
    ClientID seller_id;
    Quantity quantity;
    Price price;
    OrderSide aggressor_side{};
};

using Event = std::variant<OrderSubmitted, CancellationSubmitted, ModificationSubmitted,
                           AgentWakeup, OrderAccepted, OrderRejected, OrderCancelled,
                           OrderModified, Trade>;

constexpr Timestamp get_timestamp(const Event& event) {
    return std::visit(
        [](const auto& e) {
            return e.timestamp;
        },
        event);
}
