#pragma once

#include "exchange/types.hpp"
#include "utils/types.hpp"

#include <cstdint>

enum class DeltaType : std::uint8_t { ADD = 0, FILL, CANCEL, MODIFY };

struct OrderDelta {
    Timestamp timestamp;
    EventSequenceNumber sequence_num;
    DeltaType type;
    OrderID order_id;
    ClientID client_id;
    InstrumentID instrument_id;
    OrderSide side;
    Price price;
    Quantity quantity;
    Quantity remaining_qty;
    // Optional fields (0 if not applicable)
    TradeID trade_id{0};        // FILL only
    OrderID new_order_id{0};    // MODIFY only
    Price new_price{0};         // MODIFY only
    Quantity new_quantity{0};   // MODIFY only
};

struct TradeRecord {
    Timestamp timestamp;
    TradeID trade_id;
    InstrumentID instrument_id;
    ClientID buyer_id;
    ClientID seller_id;
    OrderID buyer_order_id;
    OrderID seller_order_id;
    Price price;
    Quantity quantity;
    OrderSide aggressor_side;
    Price fair_price;
};

struct PnLSnapshot {
    Timestamp timestamp;
    ClientID client_id;
    Quantity long_position;
    Quantity short_position;
    Cash cash;
    Price fair_price;
};

struct MarketStateSnapshot {
    Timestamp timestamp;
    Price fair_price;
    Price best_bid;   // 0 if no bids
    Price best_ask;   // 0 if no asks
};

[[nodiscard]] constexpr const char* delta_type_to_string(DeltaType type) {
    switch (type) {
        case DeltaType::ADD: return "ADD";
        case DeltaType::FILL: return "FILL";
        case DeltaType::CANCEL: return "CANCEL";
        case DeltaType::MODIFY: return "MODIFY";
    }
    return "UNKNOWN";
}

[[nodiscard]] constexpr const char* order_side_to_string(OrderSide side) {
    switch (side) {
        case OrderSide::BUY: return "BUY";
        case OrderSide::SELL: return "SELL";
    }
    return "UNKNOWN";
}
