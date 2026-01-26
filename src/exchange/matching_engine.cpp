#include "exchange/matching_engine.hpp"

[[nodiscard]] MatchResult MatchingEngine::process_order(const OrderRequest& request) {
    int side_idx = request.side == OrderSide::BUY ? 0 : 1;
    int type_idx = request.type == OrderType::LIMIT ? 0 : 1;
    return (this->*dispatch_table_[side_idx][type_idx])(request);
}

void MatchingEngine::add_to_book_(const OrderRequest& request,
                                  Quantity remaining_quantity, Price best_price,
                                  OrderStatus status) {
    Order order{.order_id = get_current_order_id(),
                .client_id = request.client_id,
                .quantity = remaining_quantity,
                .price = best_price,
                .good_till = request.good_till,
                .timestamp = Timestamp{0},
                .instrument_id = instrumentID_,
                .time_in_force = request.time_in_force,
                .side = request.side,
                .type = request.type,
                .status = status};

    if (request.side == OrderSide::BUY) {
        auto& queue = book_.bids[best_price];
        queue.emplace_back(std::move(order));
        book_.registry[queue.back().order_id] = &queue.back();
    } else {
        auto& queue = book_.asks[best_price];
        queue.emplace_back(std::move(order));
        book_.registry[queue.back().order_id] = &queue.back();
    }
}

std::optional<const Order> MatchingEngine::get_order(const OrderID order_id) const {
    if (auto it = book_.registry.find(order_id); it != book_.registry.end()) {
        return *(it->second);
    } else {
        return std::nullopt;
    }
}

bool MatchingEngine::cancel_order(const ClientID client_id, const OrderID order_id) {
    auto it = book_.registry.find(order_id);
    if (it == book_.registry.end()) {
        return false;
    }

    if (it->second->client_id != client_id) {
        return false;
    }

    return it->second->side == OrderSide::BUY
               ? remove_from_book_(order_id, it->second->price, book_.bids)
               : remove_from_book_(order_id, it->second->price, book_.asks);
}
