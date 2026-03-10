#pragma once

#include "exchange/types.hpp"
#include "persistence/csv_writer.hpp"
#include "persistence/db_writer.hpp"
#include "persistence/metadata_writer.hpp"
#include "persistence/records.hpp"
#include "persistence/writer_config.hpp"
#include "simulation/events.hpp"
#include "utils/types.hpp"

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <unordered_map>

// Forward declare PnL to avoid circular dependency
struct PnL;

class DataCollector {
public:
    DataCollector(const std::filesystem::path& output_dir,
                  Timestamp pnl_snapshot_interval = Timestamp{100},
                  WriterConfig writer_config = {})
        : pnl_snapshot_interval_(pnl_snapshot_interval), output_dir_(output_dir) {
        const bool use_csv = writer_config.backend == PersistenceBackend::CSV ||
                             writer_config.backend == PersistenceBackend::Both;
        const bool use_db = writer_config.backend == PersistenceBackend::Postgres ||
                            writer_config.backend == PersistenceBackend::Both;
        if (use_csv) {
            csv_writer_.emplace(output_dir);
        }
        if (use_db) {
            run_id_ = generate_run_id_();
            db_writer_.emplace(writer_config, run_id_);
            write_run_id_file_();
        }
    }

    // Called when an order is accepted and added to the book
    void on_order_accepted(const OrderAccepted& event, const Order& order) {
        write_delta_(OrderDelta{.timestamp = event.timestamp,
                                .sequence_num = next_sequence(),
                                .type = DeltaType::ADD,
                                .order_id = event.order_id,
                                .client_id = event.agent_id,
                                .instrument_id = event.instrument_id,
                                .side = order.side,
                                .price = order.price,
                                .quantity = order.quantity,
                                .remaining_qty = order.quantity});
    }

    // Called when a trade occurs - records the trade and FILL deltas for both sides
    void on_trade(const Trade& trade, Price fair_price) {
        write_trade_(TradeRecord{.timestamp = trade.timestamp,
                                 .trade_id = trade.trade_id,
                                 .instrument_id = trade.instrument_id,
                                 .buyer_id = trade.buyer_id,
                                 .seller_id = trade.seller_id,
                                 .buyer_order_id = trade.buyer_order_id,
                                 .seller_order_id = trade.seller_order_id,
                                 .price = trade.price,
                                 .quantity = trade.quantity,
                                 .aggressor_side = trade.aggressor_side,
                                 .fair_price = fair_price});
    }

    // Called after a fill to record the FILL delta for a specific order
    void on_fill(const Trade& trade, OrderID filled_order_id, ClientID client_id,
                 Quantity remaining_qty, OrderSide side) {
        write_delta_(OrderDelta{.timestamp = trade.timestamp,
                                .sequence_num = next_sequence(),
                                .type = DeltaType::FILL,
                                .order_id = filled_order_id,
                                .client_id = client_id,
                                .instrument_id = trade.instrument_id,
                                .side = side,
                                .price = trade.price,
                                .quantity = trade.quantity,
                                .remaining_qty = remaining_qty,
                                .trade_id = trade.trade_id});
    }

    // Called when an order is cancelled
    void on_order_cancelled(const OrderCancelled& event, const Order& order) {
        write_delta_(OrderDelta{.timestamp = event.timestamp,
                                .sequence_num = next_sequence(),
                                .type = DeltaType::CANCEL,
                                .order_id = event.order_id,
                                .client_id = event.agent_id,
                                .instrument_id = order.instrument_id,
                                .side = order.side,
                                .price = order.price,
                                .quantity = order.quantity,
                                .remaining_qty = event.remaining_quantity});
    }

    // Called when an order is modified
    void on_order_modified(const OrderModified& event, InstrumentID instrument_id,
                           OrderSide side) {
        write_delta_(OrderDelta{.timestamp = event.timestamp,
                                .sequence_num = next_sequence(),
                                .type = DeltaType::MODIFY,
                                .order_id = event.old_order_id,
                                .client_id = event.agent_id,
                                .instrument_id = instrument_id,
                                .side = side,
                                .price = event.old_price,
                                .quantity = event.old_quantity,
                                .remaining_qty = event.new_quantity,
                                .new_order_id = event.new_order_id,
                                .new_price = event.new_price,
                                .new_quantity = event.new_quantity});
    }

    // Called periodically to snapshot P&L
    template <typename PnLMap>
    void maybe_snapshot_pnl(Timestamp now, const PnLMap& pnls, Price fair_price) {
        if (now < last_pnl_snapshot_ + pnl_snapshot_interval_) {
            return;
        }
        last_pnl_snapshot_ = now;

        for (const auto& [client_id, pnl] : pnls) {
            write_pnl_(PnLSnapshot{.timestamp = now,
                                   .client_id = client_id,
                                   .long_position = pnl.long_position,
                                   .short_position = pnl.short_position,
                                   .cash = pnl.cash,
                                   .fair_price = fair_price});
        }
    }

    // Called after book-changing events to record market state
    void on_market_state(Timestamp now, Price fair_price, const OrderBook& book) {
        Price best_bid{0};
        Price best_ask{0};

        if (!book.bids.empty()) {
            best_bid = book.bids.begin()->first;
        }
        if (!book.asks.empty()) {
            best_ask = book.asks.begin()->first;
        }

        write_market_state_(MarketStateSnapshot{.timestamp = now,
                                                .fair_price = fair_price,
                                                .best_bid = best_bid,
                                                .best_ask = best_ask});
    }

    MetadataWriter& metadata() { return metadata_; }

    void finalize(Timestamp duration) {
        metadata_.set_duration(duration);
        metadata_.write(output_dir_);
        if (csv_writer_) {
            csv_writer_->flush();
        }
        if (db_writer_) {
            db_writer_->flush();
            db_writer_->finalize_run(metadata_.as_json());
        }
    }

private:
    std::optional<CSVWriter> csv_writer_;
    std::optional<DBWriter> db_writer_;
    MetadataWriter metadata_;
    EventSequenceNumber sequence_{0};
    Timestamp pnl_snapshot_interval_;
    Timestamp last_pnl_snapshot_{0};
    std::filesystem::path output_dir_;
    std::string run_id_;

    EventSequenceNumber next_sequence() { return sequence_++; }

    void write_delta_(const OrderDelta& d) {
        if (csv_writer_) { csv_writer_->write_delta(d); }
        if (db_writer_)  { db_writer_->write_delta(d); }
    }

    void write_trade_(const TradeRecord& t) {
        if (csv_writer_) { csv_writer_->write_trade(t); }
        if (db_writer_)  { db_writer_->write_trade(t); }
    }

    void write_pnl_(const PnLSnapshot& p) {
        if (csv_writer_) { csv_writer_->write_pnl(p); }
        if (db_writer_)  { db_writer_->write_pnl(p); }
    }

    void write_market_state_(const MarketStateSnapshot& m) {
        if (csv_writer_) { csv_writer_->write_market_state(m); }
        if (db_writer_)  { db_writer_->write_market_state(m); }
    }

    [[nodiscard]] static std::string generate_run_id_() {
        std::random_device rd;
        std::mt19937 gen{rd()};
        std::uniform_int_distribution<std::uint32_t> byte_dist{0, 255};

        std::array<std::uint8_t, 16> b{};
        for (auto& byte : b) {
            byte = static_cast<std::uint8_t>(byte_dist(gen));
        }
        b[6] = static_cast<std::uint8_t>((b[6] & 0x0fU) | 0x40U); // version 4
        b[8] = static_cast<std::uint8_t>((b[8] & 0x3fU) | 0x80U); // variant 10xx

        return std::format(
            "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}"
            "-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
            b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    }

    void write_run_id_file_() const {
        std::filesystem::create_directories(output_dir_);
        std::ofstream f{output_dir_ / "run_id.txt"};
        f << run_id_ << "\n";
    }
};
