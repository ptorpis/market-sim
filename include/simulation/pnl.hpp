#pragma once

#include "utils/types.hpp"

[[nodiscard]] inline Cash to_signed(Quantity q) {
    return Cash{static_cast<std::int64_t>(q.value())};
}

[[nodiscard]] inline Cash multiply_price(Cash position, Price price) {
    return Cash{position.value() * static_cast<std::int64_t>(price.value())};
}

struct PnL {
    Quantity long_position{0};
    Quantity short_position{0};
    Cash cash{0};

    [[nodiscard]] Cash net_position() const {
        return to_signed(long_position) - to_signed(short_position);
    }

    [[nodiscard]] Cash unrealized_pnl(Price mark_price) const {
        return multiply_price(net_position(), mark_price);
    }

    [[nodiscard]] Cash total_pnl(Price mark_price) const {
        return cash + unrealized_pnl(mark_price);
    }
};
