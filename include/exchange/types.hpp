#pragma once

#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

#include "utils/types.hpp"

enum class OrderStatus : uint8_t {
    PENDING = 0x00,
    NEW = 0x01,
    REJECTED = 0x02,
    PARTIALLY_FILLED = 0x03,
    FILLED = 0x04,
    CANCELLED = 0x05,
    MODIFIED = 0x06
};

enum class OrderType : std::uint8_t { LIMIT = 0, MARKET };

enum class OrderSide : std::uint8_t { BUY = 0, SELL };

enum class TimeInForce : std::uint8_t {
    GOOD_TILL_CANCELLED,
    FILL_OR_KILL,
    END_OF_DAY,
    GOOD_TILL_DATE
};

enum class ModifyStatus : std::uint8_t { ACCEPTED = 0, INVALID };

struct MatchResult {
    OrderID order_id;
    Timestamp timestamp;
    Quantity remaining_quantity;
    Price accepted_price;
    OrderStatus status;
    InstrumentID instrument_id;
    std::vector<TradeEvent> trade_vec;
};

struct TradeEvent {
    TradeID trade_id;
    OrderID buyer_order_id;
    OrderID seller_order_id;
    ClientID buyer_id;
    ClientID seller_id;
    Quantity quantity;
    Price price;
    Timestamp timestamp;
    InstrumentID instrument_id;
};

struct Order {
    const OrderID order_id;           // 8 bytes
    const ClientID client_id;         // 8 bytes
    Quantity quantity;                // 8 bytes
    const Price price;                // 8 bytes
    const Timestamp good_till;        // 8 bytes
    const Timestamp timestamp;        // 8 bytes
    const InstrumentID instrument_id; // 4 bytes
    const TimeInForce time_in_force;  // 1 byte
    const OrderSide side;             // 1 byte
    const OrderType type;             // 1 byte
    OrderStatus status;               // 1 byte
};

struct ModifyResult {
    ClientID client_id;
    OrderID old_order_id;
    OrderID new_order_id;
    Quantity new_quantity;
    Price new_price;
    ModifyStatus status;
    InstrumentID instrument_id;

    std::optional<MatchResult> match_result;
};

struct OrderRequest {
    ClientID client_id;
    Quantity quantity;
    Price price;
    InstrumentID InstrumentID;
    OrderSide side;
    OrderType type;
    TimeInForce time_in_force;
};

struct OrderBook {
    std::map<Price, std::deque<Order>, std::less<Price>> asks;
    std::map<Price, std::deque<Order>, std::greater<Price>> bids;
    std::unordered_map<OrderID, Order*, strong_hash<OrderID>> registry;
};