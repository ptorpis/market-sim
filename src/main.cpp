#include "exchange/matching_engine.hpp"
#include "exchange/types.hpp"
#include "utils/types.hpp"

#include <iostream>

int main() {
    OrderRequest req = OrderRequest{.client_id = ClientID{1},
                                    .quantity = Quantity{100},
                                    .price = Price{1000},
                                    .instrument_id = InstrumentID{1},
                                    .side = OrderSide::BUY,
                                    .type = OrderType::LIMIT};

    OrderRequest sell_req =
        OrderRequest{.client_id = ClientID{1},
                     .quantity = Quantity{100},
                     .price = Price{999},
                     .instrument_id = InstrumentID{1},
                     .side = OrderSide::SELL,
                     .type = OrderType::LIMIT};

    MatchingEngine engine{InstrumentID{1}};

    auto res = engine.process_order(req);
    [[maybe_unused]] auto res2 = engine.process_order(sell_req);

    std::cout << "remaining:" << res.remaining_quantity << std::endl;

    engine.print_order_book(5);

    return 0;
}
