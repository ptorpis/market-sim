#include "exchange/matching_engine.hpp"

MatchResult MatchingEngine::process_order(const OrderRequest& request) {
    int side_idx = request.side == OrderSide::BUY ? 0 : 1;
    int type_idx = request.type == OrderType::LIMIT ? 0 : 1;
    return (this->*dispatch_table_[side_idx][type_idx])(request);
}

void MatchingEngine::add_to_book_(const OrderRequest& request,
                                  Quantity remaining_quantity, OrderStatus status) {
    Order order{.order_id = get_current_order_id_(),
                .client_id = request.client_id,
                .quantity = remaining_quantity,
                .price = request.price,
                .good_till = request.good_till,
                .timestamp = Timestamp{0},
                .instrument_id = instrumentID_,
                .time_in_force = request.time_in_force,
                .side = request.side,
                .type = request.type,
                .status = status};

    if (request.side == OrderSide::BUY) {
        auto& queue = book_.bids[request.price];
        queue.emplace_back(std::move(order));
        book_.registry[queue.back().order_id] = &queue.back();
    } else {
        auto& queue = book_.asks[request.price];
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

ModifyResult MatchingEngine::modify_order(const ClientID client_id,
                                          const OrderID order_id,
                                          const Quantity new_quantity,
                                          const Price new_price) {
    auto it = book_.registry.find(order_id);
    if (it == book_.registry.end()) {
        return {.client_id = client_id,
                .old_order_id = order_id,
                .new_order_id = OrderID{0},
                .new_quantity = new_quantity,
                .new_price = new_price,
                .status = ModifyStatus::INVALID,
                .instrument_id = instrumentID_,
                .match_result = std::nullopt};
    }

    auto& order = it->second;
    if (order->client_id != client_id) {
        return {.client_id = client_id,
                .old_order_id = order_id,
                .new_order_id = OrderID{0},
                .new_quantity = new_quantity,
                .new_price = new_price,
                .status = ModifyStatus::INVALID,
                .instrument_id = instrumentID_,
                .match_result = std::nullopt};
    }

    if (new_price == order->price && new_quantity == order->quantity) {
        return {.client_id = client_id,
                .old_order_id = order_id,
                .new_order_id = order_id,
                .new_quantity = new_quantity,
                .new_price = new_price,
                .status = ModifyStatus::ACCEPTED,
                .instrument_id = instrumentID_,
                .match_result = std::nullopt};
    }

    if (new_price == order->price && new_quantity < order->quantity) {
        order->quantity = new_quantity;
        order->status = OrderStatus::MODIFIED;

        return {.client_id = client_id,
                .old_order_id = order_id,
                .new_order_id = order_id,
                .new_quantity = new_quantity,
                .new_price = new_price,
                .status = ModifyStatus::ACCEPTED,
                .instrument_id = instrumentID_,
                .match_result = std::nullopt};
    }

    OrderSide tmp_side = order->side;
    TimeInForce tmp_tif = order->time_in_force;
    Timestamp tmp_good_till = order->good_till;

    if (!cancel_order(client_id, order_id)) {
        return {.client_id = client_id,
                .old_order_id = order_id,
                .new_order_id = OrderID{0},
                .new_quantity = new_quantity,
                .new_price = new_price,
                .status = ModifyStatus::INVALID,
                .instrument_id = instrumentID_,
                .match_result = std::nullopt};
    }

    // now, the order is cancelled, and the pointer [order] is invalidated

    OrderRequest new_request{.client_id = client_id,
                             .quantity = new_quantity,
                             .price = new_price,
                             .instrumentID = instrumentID_,
                             .side = tmp_side,
                             .type = OrderType::LIMIT,
                             .time_in_force = tmp_tif,
                             .good_till = tmp_good_till};

    int sideIdx = new_request.side == OrderSide::BUY ? 0 : 1;
    int typeIdx = 0; // always limit

    MatchResult matchResult = (this->*dispatch_table_[sideIdx][typeIdx])(new_request);

    // now the newOrder has been moved into the book, and ownership has been handed over
    return {.client_id = client_id,
            .old_order_id = order_id,
            .new_order_id = matchResult.order_id,
            .new_quantity = new_quantity,
            .new_price = new_price,
            .status = ModifyStatus::ACCEPTED,
            .instrument_id = instrumentID_,
            .match_result = matchResult};
}
