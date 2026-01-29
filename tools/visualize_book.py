#!/usr/bin/env python3
"""
Order book visualizer for market-sim deltas.csv output.

Reconstructs and navigates order book state at any timestamp by replaying
delta events. Supports interactive stepping through time with efficient
forward and backward navigation.

Usage:
    python visualize_book.py deltas.csv                    # Show final state
    python visualize_book.py deltas.csv --at 1000          # Show state at timestamp 1000
    python visualize_book.py deltas.csv -i                 # Interactive mode
    python visualize_book.py deltas.csv --plot             # Show depth chart

Architecture:
    The tool uses a streaming approach to handle large delta files efficiently:

    1. DeltaIndex: On startup, builds a lightweight index mapping timestamps to
       byte offsets in the file. This requires a single pass through the file
       but stores only O(unique timestamps) data, not the deltas themselves.

    2. On-demand reading: When navigating to a timestamp, deltas are read
       directly from the file by seeking to the stored byte offset, avoiding
       full file scans for sequential navigation.

    3. Reverse deltas: Stepping backward applies inverse operations to undo
       deltas, enabling O(d) backward steps instead of O(N) rebuilds. This
       reduces sequential backward traversal from O(N^2) to O(N).

    4. order_add_timestamps: Tracks when each order was originally added,
       allowing correct timestamp restoration when reversing FILL, CANCEL,
       and MODIFY operations.

Complexity:
    - Build index:              O(N) single pass
    - Jump to timestamp:        O(N) rebuild from start
    - Step forward:             O(d) apply deltas at next timestamp
    - Step backward:            O(d) reverse deltas at current timestamp
    - Sequential forward scan:  O(N)
    - Sequential backward scan: O(N) with reverse deltas

    Where N = total deltas, d = deltas at one timestamp.
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
    timestamp: int


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
        self.order_add_timestamps: dict[int, int] = {}
        self.timestamp = 0

    def _get_book(self, side: Side) -> SortedDict:
        return self.bids if side == Side.BUY else self.asks

    def _add_order(self, order: Order) -> None:
        book = self._get_book(order.side)
        if order.price not in book:
            book[order.price] = deque()
        book[order.price].append(order)
        self.registry[order.order_id] = (order.price, order.side)

    def _add_order_sorted(self, order: Order) -> None:
        """Add order in correct queue position based on timestamp (FIFO order)."""
        book = self._get_book(order.side)
        if order.price not in book:
            book[order.price] = deque()
            book[order.price].append(order)
        else:
            queue = book[order.price]
            # Find correct position: orders added earlier should be ahead
            insert_pos = 0
            for i, existing in enumerate(queue):
                if existing.timestamp > order.timestamp:
                    insert_pos = i
                    break
                insert_pos = i + 1
            queue.insert(insert_pos, order)
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
        """
        Apply a forward delta to update the order book state.

        Handles ADD, FILL, CANCEL, and MODIFY delta types. Updates the book's
        timestamp and tracks order creation times for reverse delta support.
        """
        self.timestamp = int(delta["timestamp"])
        delta_type = delta["delta_type"]
        order_id = int(delta["order_id"])
        side = Side(delta["side"])
        price = int(delta["price"])
        remaining = int(delta["remaining_qty"])
        client_id = int(delta["client_id"])

        if delta_type == "ADD":
            order = Order(order_id, client_id, side, price, remaining, self.timestamp)
            self._add_order(order)
            self.order_add_timestamps[order_id] = self.timestamp

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
            new_order = Order(
                new_order_id, client_id, side, new_price, new_quantity, self.timestamp
            )
            self._add_order(new_order)
            self.order_add_timestamps[new_order_id] = self.timestamp

    def apply_reverse_delta(self, delta: dict, prev_timestamp: int) -> None:
        """
        Apply a reverse delta to revert the order book to its previous state.

        Undoes the effect of a forward delta by performing the inverse operation.
        For ADD, removes the order. For FILL, restores the previous quantity or
        re-adds fully filled orders. For CANCEL, re-adds the order. For MODIFY,
        removes the new order and restores the original.

        Args:
            delta: The delta dict to reverse.
            prev_timestamp: The timestamp to restore the book to.
        """
        delta_type = delta["delta_type"]
        order_id = int(delta["order_id"])
        side = Side(delta["side"])
        price = int(delta["price"])
        quantity = int(delta["quantity"])
        remaining = int(delta["remaining_qty"])
        client_id = int(delta["client_id"])

        if delta_type == "ADD":
            self._remove_order(order_id)
            self.order_add_timestamps.pop(order_id, None)

        elif delta_type == "FILL":
            prev_quantity = remaining + quantity
            if remaining == 0:
                # Only re-add if this order was previously in the book.
                # Aggressor orders that matched immediately were never added
                # and should not be restored.
                if order_id in self.order_add_timestamps:
                    orig_ts = self.order_add_timestamps[order_id]
                    order = Order(order_id, client_id, side, price, prev_quantity, orig_ts)
                    # Insert in correct position based on timestamp (FIFO order)
                    self._add_order_sorted(order)
            else:
                self._update_order_quantity(order_id, prev_quantity)

        elif delta_type == "CANCEL":
            orig_ts = self.order_add_timestamps.get(order_id, prev_timestamp)
            order = Order(order_id, client_id, side, price, remaining, orig_ts)
            self._add_order_sorted(order)

        elif delta_type == "MODIFY":
            new_order_id = int(delta["new_order_id"])

            self._remove_order(new_order_id)
            self.order_add_timestamps.pop(new_order_id, None)

            orig_ts = self.order_add_timestamps.get(order_id, prev_timestamp)
            order = Order(order_id, client_id, side, price, quantity, orig_ts)
            self._add_order_sorted(order)

        self.timestamp = prev_timestamp

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
        print(f"{'-' * 23}+{'-' * 23}")

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
    """Yield delta dicts from a CSV file using the standard csv.DictReader."""
    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            yield row


class DeltaIndex:
    """
    Lightweight index for streaming navigation through a deltas file.

    Builds an index mapping timestamp indices to byte offsets in the file,
    allowing on-demand reading without loading everything into memory.
    """

    def __init__(self, path: str):
        self.path = path
        self.timestamps: list[int] = []
        self._offsets: list[int] = []
        self._header_end: int = 0
        self._fieldnames: list[str] = []
        self._build_index()

    def _build_index(self) -> None:
        """
        Build the timestamp-to-offset index with a single pass through the file.

        Stores byte offsets for the first line of each unique timestamp,
        enabling efficient seeking for on-demand delta reading.
        """
        with open(self.path, "r", encoding="utf-8") as f:
            header_line = f.readline()
            self._header_end = f.tell()
            self._fieldnames = header_line.strip().split(",")

            current_ts: Optional[int] = None
            while True:
                offset = f.tell()
                line = f.readline()
                if not line:
                    break

                ts = int(line.split(",", 1)[0])
                if ts != current_ts:
                    self.timestamps.append(ts)
                    self._offsets.append(offset)
                    current_ts = ts

    def __len__(self) -> int:
        return len(self.timestamps)

    def read_deltas_at_index(self, idx: int) -> list[dict]:
        """
        Read all deltas for the timestamp at the given index.

        Seeks directly to the stored byte offset and reads until the timestamp
        changes, avoiding a full file scan.
        """
        if idx < 0 or idx >= len(self.timestamps):
            return []

        target_ts = self.timestamps[idx]
        start_offset = self._offsets[idx]

        results = []
        with open(self.path, "r", encoding="utf-8") as f:
            f.seek(start_offset)
            while True:
                line = f.readline()
                if not line:
                    break
                parts = line.strip().split(",")
                ts = int(parts[0])
                if ts != target_ts:
                    break
                results.append(dict(zip(self._fieldnames, parts)))
        return results

    def read_deltas_up_to_index(self, idx: int):
        """
        Yield all deltas from the start of the file up to and including the given index.

        Used for rebuilding the order book from scratch when jumping to a
        non-adjacent timestamp.
        """
        if idx < 0 or idx >= len(self.timestamps):
            return

        end_ts = self.timestamps[idx]
        with open(self.path, "r", encoding="utf-8") as f:
            f.seek(self._header_end)
            while True:
                line = f.readline()
                if not line:
                    break
                parts = line.strip().split(",")
                ts = int(parts[0])
                if ts > end_ts:
                    break
                yield dict(zip(self._fieldnames, parts))

    def find_timestamp_index(self, target_ts: int) -> int:
        """
        Find the index of a timestamp, or the closest timestamp if not found.

        Returns the exact index if the timestamp exists, otherwise returns
        the index of the timestamp with minimum absolute difference.
        """
        if target_ts in self.timestamps:
            return self.timestamps.index(target_ts)
        closest = min(self.timestamps, key=lambda t: abs(t - target_ts))
        return self.timestamps.index(closest)


def reconstruct_at(deltas_path: str, target_timestamp: int) -> OrderBook:
    """Reconstruct the order book state at a specific timestamp by replaying deltas."""
    book = OrderBook()
    for delta in read_deltas(deltas_path):
        if int(delta["timestamp"]) > target_timestamp:
            break
        book.apply_delta(delta)
    return book


def get_all_timestamps(deltas_path: str) -> list[int]:
    """Return a sorted list of all unique timestamps in the deltas file."""
    timestamps = set()
    for delta in read_deltas(deltas_path):
        timestamps.add(int(delta["timestamp"]))
    return sorted(timestamps)


def print_commands() -> None:
    """Print the available interactive mode commands."""
    print("Commands:")
    print("  <timestamp>          - Jump to timestamp")
    print("  n                    - Next timestamp")
    print("  p                    - Previous timestamp")
    print("  o <order_id>         - Inspect specific order")
    print("  l <BUY|SELL> <price> - List orders at price level")
    print("  q                    - Quit")
    print("  h                    - Print commands available")


def interactive_mode(deltas_path: str) -> None:
    """
    Run an interactive session for navigating the order book through time.

    Builds a lightweight index on startup, then allows stepping forward/backward
    through timestamps or jumping to specific points. Uses streaming reads and
    reverse deltas to minimize memory usage.
    """
    print("Building index...")
    index = DeltaIndex(deltas_path)
    if len(index) == 0:
        print("No deltas found in file.")
        return

    print(
        f"Found {len(index)} unique timestamps: "
        f"{index.timestamps[0]} to {index.timestamps[-1]}"
    )
    print_commands()

    def rebuild_to_index(target_idx: int) -> OrderBook:
        """Rebuild the order book from scratch by streaming up to target index."""
        new_book = OrderBook()
        for delta in index.read_deltas_up_to_index(target_idx):
            new_book.apply_delta(delta)
        return new_book

    idx = 0
    book = rebuild_to_index(0)
    current_deltas: list[dict] = index.read_deltas_at_index(0)

    while True:
        book.print_book()
        print(f"\n[{idx + 1}/{len(index)}] Timestamp: {index.timestamps[idx]}")

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
            if idx < len(index) - 1:
                idx += 1
                current_deltas = index.read_deltas_at_index(idx)
                for delta in current_deltas:
                    book.apply_delta(delta)
        elif parts[0].lower() == "p":
            if idx > 0:
                prev_ts = index.timestamps[idx - 1]
                for delta in reversed(current_deltas):
                    book.apply_reverse_delta(delta, prev_ts)
                idx -= 1
                current_deltas = index.read_deltas_at_index(idx)

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
                    print(f"  Added at: {order.timestamp}")
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
            new_idx = index.find_timestamp_index(target)
            if index.timestamps[new_idx] != target:
                print(
                    f"Timestamp {target} not found, "
                    f"showing closest: {index.timestamps[new_idx]}"
                )
            book = rebuild_to_index(new_idx)
            current_deltas = index.read_deltas_at_index(new_idx)
            idx = new_idx
        elif parts[0].lower() == "h":
            print_commands()
        else:
            print("Unknown command")


def plot_depth(book: OrderBook, output_path: Optional[str] = None) -> None:
    """
    Plot the order book depth chart showing cumulative bid/ask quantities.

    Displays bids in green and asks in red as step functions. If output_path
    is provided, saves to file instead of displaying interactively.
    """
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


def main() -> None:
    """Entry point for the order book visualizer CLI."""
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
