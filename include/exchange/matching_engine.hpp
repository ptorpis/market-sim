#pragma once
#include "exchange/types.hpp"
#include <deque>
#include <map>
#include <unordered_map>

template <typename T>
concept SidePolicy = requires(T t) {
    t.book();
    t.price_passes();
    t.is_buyer();
};

template <typename T>
concept OrderTypePolicy = requires(T t) {
    t.needs_price_check();
    t.finalize();
};

class MatchingEngine {
public:
    MatchingEngine() {}

    MatchResult process_order(const OrderRequest& request);
    bool cancel_order(const ClientID, const OrderID);
    ModifyResult modify_order(const ClientID, const OrderID, const Quantity new_qty,
                              const Price new_price);

    void reset();

private:
    InstrumentID instrumentID_;
    OrderBook book_;
    TradeID trade_counter_{0};
    OrderID order_counter_{0};

    using MatchFunction = MatchResult (MatchingEngine::*)(const OrderRequest&);
    MatchFunction dispatch_table_[2][2];

    template <typename SidePolicy, typename TypePolicy>
    MatchResult match_order_(const OrderRequest&);

    [[nodiscard]] OrderID get_next_order_id_() noexcept { return ++order_counter_; }
    [[nodiscard]] OrderID get_current_order_id() const noexcept { return order_counter_; }
    [[nodiscard]] TradeID get_next_trade_id() noexcept { return ++trade_counter_; }
    [[nodiscard]] TradeID get_current_trade_id() const noexcept { return trade_counter_; }

    void add_to_book_(const OrderRequest& request, Quantity remaining_quantity,
                      OrderStatus status);

    template <typename Book>
    bool remove_from_book_(const OrderID order_id, const Price price, Book& book);

    struct BuySide {
        constexpr static auto& book(MatchingEngine& eng) { return eng.book_.bids; }
        static bool price_passes(Price order_price, Price best_price) {
            return order_price >= best_price;
        }
        constexpr static bool is_buyer() { return true; }
    };

    struct SellSide {
        constexpr static auto& book(MatchingEngine& eng) { return eng.book_.asks; }
        static bool price_passes(Price order_price, Price best_price) {
            return order_price <= best_price;
        }
        constexpr static bool is_buyer() { return false; }
    };

    struct LimitOrderPolity {
        constexpr static bool needs_price_check() { return true; }

        static OrderStatus finalize(MatchingEngine& eng, const OrderRequest& request,
                                    Quantity remaining_quantity) {
            OrderStatus status{OrderStatus::NEW};
            if (remaining_quantity.is_zero()) {
                status = OrderStatus::FILLED;
            } else {
                if (remaining_quantity < request.quantity) {
                    status = OrderStatus::PARTIALLY_FILLED;
                }

                eng.add_to_book_(request, remaining_quantity, status);
            }

            return status;
        }
    };

    struct MarketOrderPolicy {
        constexpr static bool needs_price_check() { return false; }

        static OrderStatus finalize(MatchingEngine&, const OrderRequest& request,
                                    Quantity remaining_quantity) {
            OrderStatus status{OrderStatus::NEW};
            if (remaining_quantity.is_zero()) {
                status = OrderStatus::FILLED;
            } else if (remaining_quantity != request.quantity) {
                status = OrderStatus::PARTIALLY_FILLED;
            } else {
                status = OrderStatus::CANCELLED;
            }

            // market orders are not added to the order book as per the current policy

            return status;
        }
    };
};

template <typename SidePolicy, typename OrderTypePolicy>
MatchResult MatchingEngine::match_order_(const OrderRequest& request) {
    std::vector<TradeEvent> trade_vec{};
    Quantity remaining_quantity = request.quantity;
    const Quantity original_quantity = remaining_quantity;

    auto& book_side = SidePolicy::book(*this);

    Price best_price{};
    OrderID incoming_order_id = get_next_order_id_();

    if (book_side.empty()) {
        best_price = request.price;
    }

    while (!remaining_quantity.is_zero() && !book_side.empty()) {
        auto it = book_side.begin();
        best_price = it->first;

        if constexpr (OrderTypePolicy::needs_price_check) {
            if (!SidePolicy::price_passes(request.price, best_price)) {
                break;
            }
        }

        std::deque<Order>& queue = it->second;
        bool matched{false};

        for (auto qIt{queue.begin()}; qIt != queue.end(), remaining_quantity > 0;) {
            if (qIt->client_id == request.client_id) {
                ++qIt;
                continue;
            }

            matched = true;
            Quantity match_quantity = std::min(remaining_quantity, qIt->quantity);
            remaining_quantity -= match_quantity;
            qIt->qty -= match_quantity;

            OrderID seller_order_id{}, buyer_order_id{};
            ClientID seller_id{}, buyer_id{};

            if (SidePolicy::is_buyer()) {
                buyer_id = request.client_id;
                seller_id = qIt->client_id;
                buyer_order_id = incoming_order_id;
                seller_order_id = qIt->order_id;
            } else {
                buyer_id = qIt->client_id;
                seller_id = request.client_id;
                buyer_order_id = qIt->order_id;
                seller_order_id = incoming_order_id;
            }

            trade_vec.emplace_back(TradeEvent{.trade_id = get_next_trade_id(),
                                              .buyer_order_id = buyer_order_id,
                                              .seller_order_id = seller_order_id,
                                              .buyer_id = buyer_id,
                                              .seller_id = seller_id,
                                              .quantity = match_quantity,
                                              .price = best_price,
                                              .timestamp = Timestamp{0},
                                              .instrument_id = instrumentID_});

            if (qIt->quantity.is_zero()) {
                qIt->status = OrderStatus::FILLED;
                book_.registry.erase(qIt->order_id);
                qIt = queue.erase(qIt);
            } else {
                ++qIt;
            }
        }
        if (!matched) {
            break;
        }
        if (queue.empty()) {
            book_side.erase(best_price);
        }
    }

    OrderStatus status = OrderTypePolicy::finalize(*this, request, remaining_quantity);

    return MatchResult{.order_id = get_current_order_id(),
                       .timestamp = Timestamp{0},
                       .remaining_quantity = remaining_quantity,
                       .accepted_price = best_price,
                       .status = status,
                       .instrument_id = instrumentID_,
                       trade_vec};
}