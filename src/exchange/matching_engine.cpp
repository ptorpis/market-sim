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
                .timestamp = Timestamp{0},
                .instrument_id = instrumentID_,
                .side = request.side,
                .type = request.type,
                .status = status};

    if (request.side == OrderSide::BUY) {
        auto& queue = book_.bids[request.price];
        queue.emplace_back(std::move(order));
        book_.registry[queue.back().order_id] = {request.price, OrderSide::BUY};
    } else {
        auto& queue = book_.asks[request.price];
        queue.emplace_back(std::move(order));
        book_.registry[queue.back().order_id] = {request.price, OrderSide::SELL};
    }
}

std::optional<const Order> MatchingEngine::get_order(const OrderID order_id) const {
    auto it = book_.registry.find(order_id);
    if (it == book_.registry.end()) {
        return std::nullopt;
    }

    const auto& [price, side] = it->second;

    if (side == OrderSide::BUY) {
        auto price_it = book_.bids.find(price);
        if (price_it == book_.bids.end()) {
            return std::nullopt;
        }
        for (const auto& order : price_it->second) {
            if (order.order_id == order_id) {
                return order;
            }
        }
    } else {
        auto price_it = book_.asks.find(price);
        if (price_it == book_.asks.end()) {
            return std::nullopt;
        }
        for (const auto& order : price_it->second) {
            if (order.order_id == order_id) {
                return order;
            }
        }
    }

    return std::nullopt;
}

bool MatchingEngine::cancel_order(const ClientID client_id, const OrderID order_id) {
    auto it = book_.registry.find(order_id);
    if (it == book_.registry.end()) {
        return false;
    }

    const auto& [price, side] = it->second;

    // Look up the order to verify client_id
    auto order_opt = get_order(order_id);
    if (!order_opt || order_opt->client_id != client_id) {
        return false;
    }

    return side == OrderSide::BUY ? remove_from_book_(order_id, price, book_.bids)
                                  : remove_from_book_(order_id, price, book_.asks);
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

    const auto& [price, side] = it->second;

    // Get order to verify ownership
    auto order_opt = get_order(order_id);
    if (!order_opt || order_opt->client_id != client_id) {
        return {.client_id = client_id,
                .old_order_id = order_id,
                .new_order_id = OrderID{0},
                .new_quantity = new_quantity,
                .new_price = new_price,
                .status = ModifyStatus::INVALID,
                .instrument_id = instrumentID_,
                .match_result = std::nullopt};
    }

    if (new_price == order_opt->price && new_quantity == order_opt->quantity) {
        return {.client_id = client_id,
                .old_order_id = order_id,
                .new_order_id = order_id,
                .new_quantity = new_quantity,
                .new_price = new_price,
                .status = ModifyStatus::ACCEPTED,
                .instrument_id = instrumentID_,
                .match_result = std::nullopt};
    }

    if (new_price == order_opt->price && new_quantity < order_opt->quantity) {
        // Find and modify the order in place
        if (side == OrderSide::BUY) {
            auto price_it = book_.bids.find(price);
            if (price_it != book_.bids.end()) {
                for (auto& order : price_it->second) {
                    if (order.order_id == order_id) {
                        order.quantity = new_quantity;
                        order.status = OrderStatus::MODIFIED;
                        break;
                    }
                }
            }
        } else {
            auto price_it = book_.asks.find(price);
            if (price_it != book_.asks.end()) {
                for (auto& order : price_it->second) {
                    if (order.order_id == order_id) {
                        order.quantity = new_quantity;
                        order.status = OrderStatus::MODIFIED;
                        break;
                    }
                }
            }
        }

        return {.client_id = client_id,
                .old_order_id = order_id,
                .new_order_id = order_id,
                .new_quantity = new_quantity,
                .new_price = new_price,
                .status = ModifyStatus::ACCEPTED,
                .instrument_id = instrumentID_,
                .match_result = std::nullopt};
    }

    OrderSide tmp_side = side;

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

    // now, the order is cancelled

    OrderRequest new_request{.client_id = client_id,
                             .quantity = new_quantity,
                             .price = new_price,
                             .instrument_id = instrumentID_,
                             .side = tmp_side,
                             .type = OrderType::LIMIT};

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
