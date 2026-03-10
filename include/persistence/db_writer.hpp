#pragma once

#include "persistence/records.hpp"
#include "persistence/writer_config.hpp"

#include <nlohmann/json.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wdeprecated-literal-operator"
#include <pqxx/pqxx>
#pragma GCC diagnostic pop

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

class DBWriter {
public:
    DBWriter(const WriterConfig& config, const std::string& run_id)
        : conn_(config.connection_string), run_id_(run_id),
          batch_size_(config.batch_size) {
        pqxx::work txn{conn_};
        txn.exec_params(
            "INSERT INTO runs (run_id, config) VALUES ($1::uuid, $2::jsonb)",
            run_id_, "{}");
        txn.commit();
    }

    ~DBWriter() { flush(); }

    DBWriter(const DBWriter&) = delete;
    DBWriter& operator=(const DBWriter&) = delete;
    DBWriter(DBWriter&&) = default;
    DBWriter& operator=(DBWriter&&) = default;

    void write_delta(const OrderDelta& d) {
        auto nullable = [](auto id) -> std::optional<std::uint64_t> {
            return id.value() != 0 ? std::optional<std::uint64_t>{id.value()} : std::nullopt;
        };

        delta_buf_.emplace_back(
            run_id_,
            d.timestamp.value(),
            d.sequence_num.value(),
            std::string{delta_type_to_string(d.type)},
            d.order_id.value(),
            d.client_id.value(),
            d.instrument_id.value(),
            std::string{order_side_to_string(d.side)},
            d.price.value(),
            d.quantity.value(),
            d.remaining_qty.value(),
            nullable(d.trade_id),
            nullable(d.new_order_id),
            nullable(d.new_price),
            nullable(d.new_quantity));

        if (static_cast<int>(delta_buf_.size()) >= batch_size_) {
            flush_buf_("order_deltas",
                       {"run_id", "timestamp", "sequence_num", "delta_type", "order_id",
                        "client_id", "instrument_id", "side", "price", "quantity",
                        "remaining_qty", "trade_id", "new_order_id", "new_price",
                        "new_quantity"},
                       delta_buf_);
        }
    }

    void write_trade(const TradeRecord& t) {
        trade_buf_.emplace_back(
            run_id_,
            t.timestamp.value(),
            t.trade_id.value(),
            t.instrument_id.value(),
            t.buyer_id.value(),
            t.seller_id.value(),
            t.buyer_order_id.value(),
            t.seller_order_id.value(),
            t.price.value(),
            t.quantity.value(),
            std::string{order_side_to_string(t.aggressor_side)},
            t.fair_price.value());

        if (static_cast<int>(trade_buf_.size()) >= batch_size_) {
            flush_buf_("trades",
                       {"run_id", "timestamp", "trade_id", "instrument_id", "buyer_id",
                        "seller_id", "buyer_order_id", "seller_order_id", "price",
                        "quantity", "aggressor_side", "fair_price"},
                       trade_buf_);
        }
    }

    void write_pnl(const PnLSnapshot& p) {
        pnl_buf_.emplace_back(
            run_id_,
            p.timestamp.value(),
            p.client_id.value(),
            p.long_position.value(),
            p.short_position.value(),
            p.cash.value(),
            p.fair_price.value());

        if (static_cast<int>(pnl_buf_.size()) >= batch_size_) {
            flush_buf_("pnl_snapshots",
                       {"run_id", "timestamp", "client_id", "long_position",
                        "short_position", "cash", "fair_price"},
                       pnl_buf_);
        }
    }

    void write_market_state(const MarketStateSnapshot& m) {
        market_state_buf_.emplace_back(
            run_id_,
            m.timestamp.value(),
            m.fair_price.value(),
            m.best_bid.value(),
            m.best_ask.value());

        if (static_cast<int>(market_state_buf_.size()) >= batch_size_) {
            flush_buf_("market_state",
                       {"run_id", "timestamp", "fair_price", "best_bid", "best_ask"},
                       market_state_buf_);
        }
    }

    void flush() {
        flush_buf_("order_deltas",
                   {"run_id", "timestamp", "sequence_num", "delta_type", "order_id",
                    "client_id", "instrument_id", "side", "price", "quantity",
                    "remaining_qty", "trade_id", "new_order_id", "new_price",
                    "new_quantity"},
                   delta_buf_);
        flush_buf_("trades",
                   {"run_id", "timestamp", "trade_id", "instrument_id", "buyer_id",
                    "seller_id", "buyer_order_id", "seller_order_id", "price", "quantity",
                    "aggressor_side", "fair_price"},
                   trade_buf_);
        flush_buf_("pnl_snapshots",
                   {"run_id", "timestamp", "client_id", "long_position", "short_position",
                    "cash", "fair_price"},
                   pnl_buf_);
        flush_buf_("market_state",
                   {"run_id", "timestamp", "fair_price", "best_bid", "best_ask"},
                   market_state_buf_);
    }

    void finalize_run(const nlohmann::json& config) {
        pqxx::work txn{conn_};
        txn.exec_params("UPDATE runs SET config = $1::jsonb WHERE run_id = $2::uuid",
                        config.dump(), run_id_);
        txn.commit();
    }

private:
    // Tuple field types mirror the underlying value types returned by .value()
    using DeltaRow =
        std::tuple<std::string, std::uint64_t, std::uint64_t, std::string,
                   std::uint64_t, std::uint64_t, std::uint32_t, std::string,
                   std::uint64_t, std::uint64_t, std::uint64_t,
                   std::optional<std::uint64_t>, std::optional<std::uint64_t>,
                   std::optional<std::uint64_t>, std::optional<std::uint64_t>>;

    using TradeRow =
        std::tuple<std::string, std::uint64_t, std::uint64_t, std::uint32_t,
                   std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                   std::uint64_t, std::uint64_t, std::string, std::uint64_t>;

    using PnLRow =
        std::tuple<std::string, std::uint64_t, std::uint64_t, std::uint64_t,
                   std::uint64_t, std::int64_t, std::uint64_t>;

    using MarketStateRow =
        std::tuple<std::string, std::uint64_t, std::uint64_t, std::uint64_t,
                   std::uint64_t>;

    pqxx::connection conn_;
    std::string run_id_;
    int batch_size_;

    std::vector<DeltaRow> delta_buf_;
    std::vector<TradeRow> trade_buf_;
    std::vector<PnLRow> pnl_buf_;
    std::vector<MarketStateRow> market_state_buf_;

    template <typename RowT>
    void flush_buf_(const std::string& table, const std::vector<std::string>& columns,
                    std::vector<RowT>& buf) {
        if (buf.empty()) {
            return;
        }
        pqxx::work txn{conn_};
        const std::string quoted_cols = txn.conn().quote_columns(columns);
        auto stream = pqxx::stream_to::raw_table(txn, table, quoted_cols);
        for (const auto& row : buf) {
            stream << row;
        }
        stream.complete();
        txn.commit();
        buf.clear();
    }
};
