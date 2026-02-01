#pragma once

#include "exchange/types.hpp"
#include "utils/types.hpp"

/**
 * Represents an order tracked by an agent for stale order detection.
 */
struct TrackedOrder {
    OrderID order_id;
    Price price;
    OrderSide side;
};
