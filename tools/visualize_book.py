#!/usr/bin/env python3
"""
Order book visualizer for market-sim deltas.csv output.

Reconstructs order book state at any timestamp by replaying deltas.
"""

import argparse
import csv
from collections import deque
from dataclasses import dataclass
from enum import Enum
from typing import Optional

from sortedcontainers import SortedDict

import matplotlib.pyplot as plt


class Side(Enum):
    BUY = "BUY"
    SELL = "SELL"


@dataclass
class Order:
    order_id: int
    client_id: int
    side: Side
    price: int
    quantity: int


class OrderBook:
    """
    Reconstructs order book state by processing delta events.

    Maintains full order-level detail using SortedDict with deque at each
    price level, matching the C++ implementation. Bids are sorted descending
    (highest first), asks are sorted ascending (lowest first).
    """

    def __init__(self):
        self.asks = SortedDict()
        self.bids = SortedDict(lambda x: -x)
        self.registry: dict[int, tuple[int, Side]] = {}
        self.timestamp = 0

    def _get_book(self, side: Side) -> SortedDict:
        return self.bids if side == Side.BUY else self.asks

    def _add_order(self, order: Order) -> None:
        book = self._get_book(order.side)
        if order.price not in book:
            book[order.price] = deque()
        book[order.price].append(order)
        self.registry[order.order_id] = (order.price, order.side)

    def _remove_order(self, order_id: int) -> Optional[Order]:
        if order_id not in self.registry:
            return None

        price, side = self.registry[order_id]
        book = self._get_book(side)

        if price not in book:
            del self.registry[order_id]
            return None

        queue = book[price]
        for i, order in enumerate(queue):
            if order.order_id == order_id:
                del queue[i]
                if not queue:
                    del book[price]
                del self.registry[order_id]
                return order

        del self.registry[order_id]
        return None

    def _update_order_quantity(self, order_id: int, new_quantity: int) -> None:
        if order_id not in self.registry:
            return

        price, side = self.registry[order_id]
        book = self._get_book(side)

        if price not in book:
            return

        for order in book[price]:
            if order.order_id == order_id:
                order.quantity = new_quantity
                return

    def apply_delta(self, delta: dict) -> None:
        self.timestamp = int(delta["timestamp"])
        delta_type = delta["delta_type"]
        order_id = int(delta["order_id"])
        side = Side(delta["side"])
        price = int(delta["price"])
        remaining = int(delta["remaining_qty"])
        client_id = int(delta["client_id"])

        if delta_type == "ADD":
            order = Order(order_id, client_id, side, price, remaining)
            self._add_order(order)

        elif delta_type == "FILL":
            if remaining == 0:
                self._remove_order(order_id)
            else:
                self._update_order_quantity(order_id, remaining)

        elif delta_type == "CANCEL":
            self._remove_order(order_id)

        elif delta_type == "MODIFY":
            new_order_id = int(delta["new_order_id"])
            new_price = int(delta["new_price"])
            new_quantity = int(delta["new_quantity"])

            self._remove_order(order_id)
            new_order = Order(new_order_id, client_id, side, new_price, new_quantity)
            self._add_order(new_order)

    def get_order(self, order_id: int) -> Optional[Order]:
        if order_id not in self.registry:
            return None

        price, side = self.registry[order_id]
        book = self._get_book(side)

        if price not in book:
            return None

        for order in book[price]:
            if order.order_id == order_id:
                return order

        return None

    def get_orders_at_price(self, side: Side, price: int) -> list[Order]:
        book = self._get_book(side)
        if price in book:
            return list(book[price])
        return []

    def best_bid(self) -> Optional[tuple[int, int]]:
        if not self.bids:
            return None
        price = self.bids.keys()[0]
        total_qty = sum(o.quantity for o in self.bids[price])
        return price, total_qty

    def best_ask(self) -> Optional[tuple[int, int]]:
        if not self.asks:
            return None
        price = self.asks.keys()[0]
        total_qty = sum(o.quantity for o in self.asks[price])
        return price, total_qty

    def spread(self) -> Optional[int]:
        bb = self.best_bid()
        ba = self.best_ask()
        if bb and ba:
            return ba[0] - bb[0]
        return None

    def midpoint(self) -> Optional[float]:
        bb = self.best_bid()
        ba = self.best_ask()
        if bb and ba:
            return (bb[0] + ba[0]) / 2
        return None

    def get_depth(self, levels: int = 10) -> tuple[list, list]:
        bid_levels = []
        for i, price in enumerate(self.bids.keys()):
            if i >= levels:
                break
            total_qty = sum(o.quantity for o in self.bids[price])
            bid_levels.append((price, total_qty))

        ask_levels = []
        for i, price in enumerate(self.asks.keys()):
            if i >= levels:
                break
            total_qty = sum(o.quantity for o in self.asks[price])
            ask_levels.append((price, total_qty))

        return bid_levels, ask_levels

    def get_full_depth(self) -> tuple[list, list]:
        bid_levels = [
            (price, sum(o.quantity for o in level))
            for price, level in self.bids.items()
        ]

        ask_levels = [
            (price, sum(o.quantity for o in level))
            for price, level in self.asks.items()
        ]

        return bid_levels, ask_levels

    def print_book(self, levels: int = 10) -> None:
        bid_levels, ask_levels = self.get_depth(levels)

        print(f"\n{'=' * 47}")
        print(f" ORDER BOOK at timestamp {self.timestamp}")
        print(f"{'=' * 47}")

        mid = self.midpoint()
        spread = self.spread()
        if mid and spread:
            print(f" Midpoint: {mid:.1f}  Spread: {spread}")
        print()

        print(f"{'BID (Qty @ Price)':>22} | {'ASK (Qty @ Price)':<22}")
        print(f"{'-' * 22}+{'-' * 23}")

        max_rows = max(len(bid_levels), len(ask_levels))
        for i in range(max_rows):
            bid_str = ""
            ask_str = ""

            if i < len(bid_levels):
                price, qty = bid_levels[i]
                bid_str = f"{qty} @ {price}"

            if i < len(ask_levels):
                price, qty = ask_levels[i]
                ask_str = f"{qty} @ {price}"

            print(f"{bid_str:>22} | {ask_str:<22}")

        if not bid_levels and not ask_levels:
            print(f"{'(empty)':^47}")

    def print_orders_at_level(self, side: Side, price: int) -> None:
        orders = self.get_orders_at_price(side, price)
        if not orders:
            print(f"No {side.value} orders at price {price}")
            return

        print(f"\n{side.value} orders at price {price}:")
        print(f"{'Order ID':>12} {'Client ID':>12} {'Quantity':>12}")
        print("-" * 40)
        for order in orders:
            print(f"{order.order_id:>12} {order.client_id:>12} {order.quantity:>12}")


def read_deltas(path: str):
    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            yield row


def reconstruct_at(deltas_path: str, target_timestamp: int) -> OrderBook:
    book = OrderBook()
    for delta in read_deltas(deltas_path):
        if int(delta["timestamp"]) > target_timestamp:
            break
        book.apply_delta(delta)
    return book


def get_all_timestamps(deltas_path: str) -> list[int]:
    timestamps = set()
    for delta in read_deltas(deltas_path):
        timestamps.add(int(delta["timestamp"]))
    return sorted(timestamps)


def interactive_mode(deltas_path: str) -> None:
    timestamps = get_all_timestamps(deltas_path)
    if not timestamps:
        print("No deltas found in file.")
        return

    print(
        f"Found {len(timestamps)} unique timestamps: {timestamps[0]} to {timestamps[-1]}"
    )
    print("Commands:")
    print("  <timestamp>       - Jump to timestamp")
    print("  n                 - Next timestamp")
    print("  p                 - Previous timestamp")
    print("  o <order_id>      - Inspect specific order")
    print("  l <BUY|SELL> <price> - List orders at price level")
    print("  q                 - Quit")

    idx = 0
    book = None
    while True:
        if book is None or book.timestamp != timestamps[idx]:
            book = reconstruct_at(deltas_path, timestamps[idx])
        book.print_book()
        print(f"\n[{idx + 1}/{len(timestamps)}] Timestamp: {timestamps[idx]}")

        try:
            cmd = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        parts = cmd.split()
        if not parts:
            continue

        if parts[0].lower() == "q":
            break
        elif parts[0].lower() == "n":
            idx = min(idx + 1, len(timestamps) - 1)
        elif parts[0].lower() == "p":
            idx = max(idx - 1, 0)
        elif parts[0].lower() == "o" and len(parts) == 2:
            try:
                order_id = int(parts[1])
                order = book.get_order(order_id)
                if order:
                    print(f"\nOrder {order_id}:")
                    print(f"  Client: {order.client_id}")
                    print(f"  Side: {order.side.value}")
                    print(f"  Price: {order.price}")
                    print(f"  Quantity: {order.quantity}")
                else:
                    print(f"Order {order_id} not found")
            except ValueError:
                print("Invalid order ID")
        elif parts[0].lower() == "l" and len(parts) == 3:
            try:
                side = Side(parts[1].upper())
                price = int(parts[2])
                book.print_orders_at_level(side, price)
            except (ValueError, KeyError):
                print("Invalid side or price. Use: l BUY <price> or l SELL <price>")
        elif parts[0].isdigit():
            target = int(parts[0])
            if target in timestamps:
                idx = timestamps.index(target)
            else:
                closest = min(timestamps, key=lambda t: abs(t - target))
                idx = timestamps.index(closest)
                print(f"Timestamp {target} not found, showing closest: {closest}")
        else:
            print("Unknown command")


def plot_depth(book: OrderBook, output_path: Optional[str] = None) -> None:

    bid_levels, ask_levels = book.get_full_depth()

    if not bid_levels and not ask_levels:
        print("Order book is empty, nothing to plot.")
        return

    _, ax = plt.subplots(figsize=(12, 6))

    if bid_levels:
        bid_prices = [p for p, _ in reversed(bid_levels)]
        bid_cum_qty = []
        total = 0
        for _, qty in reversed(bid_levels):
            total += qty
            bid_cum_qty.append(total)
        bid_cum_qty = list(reversed(bid_cum_qty))
        ax.fill_between(bid_prices, bid_cum_qty, step="post", alpha=0.4, color="green")
        ax.step(bid_prices, bid_cum_qty, where="post", color="green", label="Bids")

    if ask_levels:
        ask_prices = [p for p, _ in ask_levels]
        ask_cum_qty = []
        total = 0
        for _, qty in ask_levels:
            total += qty
            ask_cum_qty.append(total)
        ax.fill_between(ask_prices, ask_cum_qty, step="post", alpha=0.4, color="red")
        ax.step(ask_prices, ask_cum_qty, where="post", color="red", label="Asks")

    ax.set_xlabel("Price")
    ax.set_ylabel("Cumulative Quantity")
    ax.set_title(f"Order Book Depth at Timestamp {book.timestamp}")
    ax.legend()
    ax.grid(True, alpha=0.3)

    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches="tight")
        print(f"Saved plot to {output_path}")
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(
        description="Visualize order book from market-sim deltas.csv"
    )
    parser.add_argument("deltas_file", help="Path to deltas.csv file")
    parser.add_argument(
        "--at",
        type=int,
        metavar="TIMESTAMP",
        help="Show order book at specific timestamp",
    )
    parser.add_argument(
        "--interactive",
        "-i",
        action="store_true",
        help="Interactive mode: step through timestamps",
    )
    parser.add_argument(
        "--plot",
        action="store_true",
        help="Show depth chart (requires matplotlib)",
    )
    parser.add_argument(
        "--plot-output",
        metavar="PATH",
        help="Save depth chart to file instead of showing",
    )
    parser.add_argument(
        "--levels",
        type=int,
        default=10,
        help="Number of price levels to show (default: 10)",
    )

    args = parser.parse_args()

    if args.interactive:
        interactive_mode(args.deltas_file)
    elif args.at is not None:
        book = reconstruct_at(args.deltas_file, args.at)
        book.print_book(args.levels)
        if args.plot or args.plot_output:
            plot_depth(book, args.plot_output)
    else:
        timestamps = get_all_timestamps(args.deltas_file)
        if timestamps:
            book = reconstruct_at(args.deltas_file, timestamps[-1])
            book.print_book(args.levels)
            if args.plot or args.plot_output:
                plot_depth(book, args.plot_output)
        else:
            print("No deltas found in file.")


if __name__ == "__main__":
    main()
