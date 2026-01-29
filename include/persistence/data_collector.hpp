#pragma once

#include "exchange/types.hpp"
#include "persistence/csv_writer.hpp"
#include "persistence/metadata_writer.hpp"
#include "persistence/records.hpp"
#include "simulation/events.hpp"
#include "utils/types.hpp"

#include <filesystem>
#include <unordered_map>

// Forward declare PnL to avoid circular dependency
struct PnL;

class DataCollector {
public:
    DataCollector(const std::filesystem::path& output_dir,
                  Timestamp pnl_snapshot_interval = Timestamp{100})
        : csv_writer_(output_dir), pnl_snapshot_interval_(pnl_snapshot_interval),
          output_dir_(output_dir) {}

    // Called when an order is accepted and added to the book
    void on_order_accepted(const OrderAccepted& event, const Order& order) {
        csv_writer_.write_delta(OrderDelta{.timestamp = event.timestamp,
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
    void on_trade(const Trade& trade) {
        csv_writer_.write_trade(TradeRecord{.timestamp = trade.timestamp,
                                            .trade_id = trade.trade_id,
                                            .instrument_id = trade.instrument_id,
                                            .buyer_id = trade.buyer_id,
                                            .seller_id = trade.seller_id,
                                            .buyer_order_id = trade.buyer_order_id,
                                            .seller_order_id = trade.seller_order_id,
                                            .price = trade.price,
                                            .quantity = trade.quantity});
    }

    // Called after a fill to record the FILL delta for a specific order
    void on_fill(const Trade& trade, OrderID filled_order_id, ClientID client_id,
                 Quantity remaining_qty, OrderSide side) {
        csv_writer_.write_delta(OrderDelta{.timestamp = trade.timestamp,
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
        csv_writer_.write_delta(OrderDelta{.timestamp = event.timestamp,
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
        csv_writer_.write_delta(OrderDelta{.timestamp = event.timestamp,
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
            csv_writer_.write_pnl(PnLSnapshot{.timestamp = now,
                                              .client_id = client_id,
                                              .long_position = pnl.long_position,
                                              .short_position = pnl.short_position,
                                              .cash = pnl.cash,
                                              .fair_price = fair_price});
        }
    }

    MetadataWriter& metadata() { return metadata_; }

    void finalize(Timestamp duration) {
        metadata_.set_duration(duration);
        metadata_.write(output_dir_);
        csv_writer_.flush();
    }

private:
    CSVWriter csv_writer_;
    MetadataWriter metadata_;
    EventSequenceNumber sequence_{0};
    Timestamp pnl_snapshot_interval_;
    Timestamp last_pnl_snapshot_{0};
    std::filesystem::path output_dir_;

    EventSequenceNumber next_sequence() { return sequence_++; }
};
