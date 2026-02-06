#pragma once

#include "persistence/records.hpp"

#include <filesystem>
#include <fstream>
#include <print>

class CSVWriter {
public:
    explicit CSVWriter(const std::filesystem::path& output_dir) {
        std::filesystem::create_directories(output_dir);
        deltas_file_.open(output_dir / "deltas.csv");
        trades_file_.open(output_dir / "trades.csv");
        pnl_file_.open(output_dir / "pnl.csv");
        market_state_file_.open(output_dir / "market_state.csv");
        if (!deltas_file_.is_open() || !trades_file_.is_open() || !pnl_file_.is_open() ||
            !market_state_file_.is_open()) {
            throw std::runtime_error("Failed to open output files in: " + output_dir.string());
        }
        write_headers();
    }

    ~CSVWriter() { flush(); }

    CSVWriter(const CSVWriter&) = delete;
    CSVWriter& operator=(const CSVWriter&) = delete;
    CSVWriter(CSVWriter&&) = default;
    CSVWriter& operator=(CSVWriter&&) = default;

    void write_delta(const OrderDelta& d) {
        std::println(deltas_file_, "{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
                     d.timestamp.value(), d.sequence_num.value(), delta_type_to_string(d.type),
                     d.order_id.value(), d.client_id.value(), d.instrument_id.value(),
                     order_side_to_string(d.side), d.price.value(), d.quantity.value(),
                     d.remaining_qty.value(), d.trade_id.value(), d.new_order_id.value(),
                     d.new_price.value(), d.new_quantity.value());
    }

    void write_trade(const TradeRecord& t) {
        std::println(trades_file_, "{},{},{},{},{},{},{},{},{},{},{}", t.timestamp.value(),
                     t.trade_id.value(), t.instrument_id.value(), t.buyer_id.value(),
                     t.seller_id.value(), t.buyer_order_id.value(), t.seller_order_id.value(),
                     t.price.value(), t.quantity.value(), order_side_to_string(t.aggressor_side),
                     t.fair_price.value());
    }

    void write_pnl(const PnLSnapshot& p) {
        std::println(pnl_file_, "{},{},{},{},{},{}", p.timestamp.value(), p.client_id.value(),
                     p.long_position.value(), p.short_position.value(), p.cash.value(),
                     p.fair_price.value());
    }

    void write_market_state(const MarketStateSnapshot& m) {
        std::println(market_state_file_, "{},{},{},{}", m.timestamp.value(),
                     m.fair_price.value(), m.best_bid.value(), m.best_ask.value());
    }

    void flush() {
        deltas_file_.flush();
        trades_file_.flush();
        pnl_file_.flush();
        market_state_file_.flush();
    }

private:
    std::ofstream deltas_file_;
    std::ofstream trades_file_;
    std::ofstream pnl_file_;
    std::ofstream market_state_file_;

    void write_headers() {
        std::println(deltas_file_,
                     "timestamp,sequence_num,delta_type,order_id,client_id,instrument_id,"
                     "side,price,quantity,remaining_qty,trade_id,new_order_id,new_price,"
                     "new_quantity");

        std::println(trades_file_,
                     "timestamp,trade_id,instrument_id,buyer_id,seller_id,"
                     "buyer_order_id,seller_order_id,price,quantity,"
                     "aggressor_side,fair_price");

        std::println(pnl_file_,
                     "timestamp,client_id,long_position,short_position,cash,fair_price");

        std::println(market_state_file_, "timestamp,fair_price,best_bid,best_ask");
    }
};
