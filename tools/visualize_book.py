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
from typing import Optional


import matplotlib.pyplot as plt


from tools.visualizer.order_book import Side, OrderBook


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
    print("  t                    - Inspect the top of the book")
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
        elif parts[0].lower() == "t":
            print("\nTOP OF BOOK:")
            if (bid := book.best_bid()) and (ask := book.best_ask()):
                best_bid_price, _ = bid
                best_ask_price, _ = ask
                book.print_orders_at_level(Side.BUY, best_bid_price)
                book.print_orders_at_level(Side.SELL, best_ask_price)

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
