#pragma once

#include "exchange/types.hpp"
#include <algorithm>
#include <deque>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
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
    MatchingEngine(InstrumentID instrument_id) : instrumentID_(instrument_id) {
        dispatch_table_[0][0] = &MatchingEngine::match_order_<BuySide, LimitOrderPolicy>;
        dispatch_table_[0][1] = &MatchingEngine::match_order_<BuySide, MarketOrderPolicy>;
        dispatch_table_[1][0] = &MatchingEngine::match_order_<SellSide, LimitOrderPolicy>;
        dispatch_table_[1][1] =
            &MatchingEngine::match_order_<SellSide, MarketOrderPolicy>;
    }

    [[nodiscard]] MatchResult process_order(const OrderRequest& request);
    [[nodiscard]] bool cancel_order(const ClientID, const OrderID);
    [[nodiscard]] ModifyResult modify_order(const ClientID, const OrderID,
                                            const Quantity new_qty,
                                            const Price new_price);

    [[nodiscard]] std::optional<const Order> get_order(const OrderID order_id) const;

    void reset();

    template <OrderSide Side>
    [[nodiscard]] std::vector<std::pair<Price, Quantity>> get_snapshot() const {
        if constexpr (Side == OrderSide::BUY) {
            return make_snapshot(book_.bids);
        } else {
            return make_snapshot(book_.asks);
        }
    }

    void print_order_book(std::size_t depth = 15) const {
        auto bids = get_snapshot<OrderSide::BUY>();
        auto asks = get_snapshot<OrderSide::SELL>();

        std::cout << "=============== ORDER BOOK ===============\n";
        std::cout << "   BID (Qty @ Price) |   ASK (Qty @ Price)\n";
        std::cout << "---------------------+---------------------\n";

        auto bid_it = bids.begin();
        auto ask_it = asks.begin();

        for (std::size_t i = 0; i < depth; ++i) {
            std::string bid_str = (bid_it != bids.end())
                                      ? (std::to_string(bid_it->second.value()) + " @ " +
                                         std::to_string(bid_it->first.value()))
                                      : "";
            std::string ask_str = (ask_it != asks.end())
                                      ? (std::to_string(ask_it->second.value()) + " @ " +
                                         std::to_string(ask_it->first.value()))
                                      : "";

            std::cout << std::setw(20) << bid_str << " | " << ask_str << "\n";

            if (bid_it != bids.end()) ++bid_it;
            if (ask_it != asks.end()) ++ask_it;
        }

        std::cout << std::flush;
    }

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
    [[nodiscard]] OrderID get_current_order_id_() const noexcept {
        return order_counter_;
    }
    [[nodiscard]] TradeID get_next_trade_id_() noexcept { return ++trade_counter_; }
    [[nodiscard]] TradeID get_current_trade_id_() const noexcept {
        return trade_counter_;
    }

    static std::vector<std::pair<Price, Quantity>> make_snapshot(const auto& book) {
        std::vector<std::pair<Price, Quantity>> snapshot;
        snapshot.reserve(book.size());

        for (const auto& [price, queue] : book) {
            auto qtyView = queue | std::views::transform([](const Order& order) {
                               return order.quantity;
                           });

            Quantity total = std::ranges::fold_left(qtyView, Quantity{0},
                                                    [](Quantity acc, Quantity q) {
                                                        return acc + q;
                                                    });

            if (!total.is_zero()) {
                snapshot.emplace_back(price, total);
            }
        }

        return snapshot;
    }

    void add_to_book_(const OrderRequest& request, Quantity remaining_quantity,
                      OrderStatus status);

    template <typename Book> [[nodiscard]] bool
    remove_from_book_(const OrderID order_id, const Price price, Book& book);

    struct BuySide {
        constexpr static auto& book(MatchingEngine& eng) { return eng.book_.asks; }
        static bool price_passes(Price order_price, Price best_price) {
            return order_price >= best_price;
        }
        constexpr static bool is_buyer() { return true; }
    };

    struct SellSide {
        constexpr static auto& book(MatchingEngine& eng) { return eng.book_.bids; }
        static bool price_passes(Price order_price, Price best_price) {
            return order_price <= best_price;
        }
        constexpr static bool is_buyer() { return false; }
    };

    struct LimitOrderPolicy {
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
    auto& book_side = SidePolicy::book(*this);

    Price best_price{};
    OrderID incoming_order_id = get_next_order_id_();

    if (book_side.empty()) {
        best_price = request.price;
    }

    while (!remaining_quantity.is_zero() && !book_side.empty()) {
        auto it = book_side.begin();
        best_price = it->first;

        if constexpr (OrderTypePolicy::needs_price_check()) {
            if (!SidePolicy::price_passes(request.price, best_price)) {
                break;
            }
        }

        std::deque<Order>& queue = it->second;
        bool matched{false};

        for (auto qIt{queue.begin()}; qIt != queue.end() && remaining_quantity > 0;) {
            if (qIt->client_id == request.client_id) {
                ++qIt;
                continue;
            }

            matched = true;
            Quantity match_quantity = std::min(remaining_quantity, qIt->quantity);
            remaining_quantity -= match_quantity;
            qIt->quantity -= match_quantity;

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

            trade_vec.emplace_back(TradeEvent{.trade_id = get_next_trade_id_(),
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

    return MatchResult{.order_id = get_current_order_id_(),
                       .timestamp = Timestamp{0},
                       .remaining_quantity = remaining_quantity,
                       .accepted_price = best_price,
                       .status = status,
                       .instrument_id = instrumentID_,
                       .trade_vec = trade_vec};
}

template <typename Book>
bool MatchingEngine::remove_from_book_(OrderID order_id, Price price, Book& book_side) {
    auto it = book_side.find(price);
    if (it == book_side.end()) {
        return false;
    }

    std::deque<Order>& queue = it->second;

    for (auto qIt = queue.begin(); qIt != queue.end(); ++qIt) {
        if (qIt->order_id != order_id) {
            continue;
        }

        book_.registry.erase(order_id);
        queue.erase(qIt);

        if (queue.empty()) {
            book_side.erase(it);
        }

        return true;
    }

    return false;
}