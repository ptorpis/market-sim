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
    python visualize_book.py deltas.csv --animate          # Animate over time
    python visualize_book.py deltas.csv --animate-output book.mp4  # Save animation

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
from dataclasses import dataclass
from typing import Optional


import matplotlib.pyplot as plt
import matplotlib.animation as animation
import matplotlib.ticker as mticker
import seaborn as sns


from tools.visualizer.order_book import Side, OrderBook
from tools.db import reader as db_reader


@dataclass
class _FrameData:
    """Lightweight snapshot of order book state for one animation frame."""

    timestamp: int
    # Depth chart (cumulative, ready to pass straight to ax.step / fill_between)
    bid_prices: list[int]
    bid_cum: list[int]
    ask_prices: list[int]
    ask_cum: list[int]
    # Tower chart (per-level, limited to top N levels around the spread)
    tower_prices: list[int]
    tower_bid_qtys: list[int]
    tower_ask_qtys: list[int]


def _build_frame(book: OrderBook, ts: int, tower_levels: int) -> _FrameData:
    """Extract the minimal drawing data from the current book state."""
    bid_levels, ask_levels = book.get_full_depth()

    bid_prices = [p for p, _ in reversed(bid_levels)]
    bid_cum: list[int] = []
    total = 0
    for _, qty in reversed(bid_levels):
        total += qty
        bid_cum.append(total)
    bid_cum = list(reversed(bid_cum))

    ask_prices = [p for p, _ in ask_levels]
    ask_cum: list[int] = []
    total = 0
    for _, qty in ask_levels:
        total += qty
        ask_cum.append(total)

    top_bids = dict(bid_levels[:tower_levels])
    top_asks = dict(ask_levels[:tower_levels])
    tower_prices = sorted(top_bids.keys() | top_asks.keys())

    return _FrameData(
        timestamp=ts,
        bid_prices=bid_prices,
        bid_cum=bid_cum,
        ask_prices=ask_prices,
        ask_cum=ask_cum,
        tower_prices=tower_prices,
        tower_bid_qtys=[top_bids.get(p, 0) for p in tower_prices],
        tower_ask_qtys=[top_asks.get(p, 0) for p in tower_prices],
    )


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


class DBDeltaIndex:
    """
    In-memory delta index backed by a PostgreSQL run.

    Loads all deltas for the given run_id at construction time and groups them
    by timestamp, providing the same navigation interface as DeltaIndex so
    that the rest of the visualizer works without modification.
    """

    def __init__(self, run_id: str, conn_str: str):
        print("Loading deltas from database...")
        all_deltas = db_reader.load_all_deltas(run_id, conn_str)

        self.timestamps: list[int] = []
        self._groups: list[list[dict]] = []

        for delta in all_deltas:
            ts = int(delta["timestamp"])
            if not self.timestamps or self.timestamps[-1] != ts:
                self.timestamps.append(ts)
                self._groups.append([])
            self._groups[-1].append(delta)

    def __len__(self) -> int:
        return len(self.timestamps)

    def read_deltas_at_index(self, idx: int) -> list[dict]:
        if idx < 0 or idx >= len(self.timestamps):
            return []
        return list(self._groups[idx])

    def read_deltas_up_to_index(self, idx: int):
        if idx < 0 or idx >= len(self.timestamps):
            return
        for i in range(idx + 1):
            yield from self._groups[i]

    def find_timestamp_index(self, target_ts: int) -> int:
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


def reconstruct_at_from_index(
    index: DeltaIndex | DBDeltaIndex,
    target_timestamp: int,
) -> OrderBook:
    """Reconstruct the order book up to target_timestamp using a pre-built index."""
    ts_idx = index.find_timestamp_index(target_timestamp)
    book = OrderBook()
    for delta in index.read_deltas_up_to_index(ts_idx):
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
    print("  d <num|max>          - Set number of levels to display")
    print("  q                    - Quit")
    print("  h                    - Print commands available")


def interactive_mode(
    deltas_path: Optional[str],
    levels: Optional[int],
    index: Optional[DeltaIndex | DBDeltaIndex] = None,
) -> None:
    """
    Run an interactive session for navigating the order book through time.

    Builds a lightweight index on startup, then allows stepping forward/backward
    through timestamps or jumping to specific points. Uses streaming reads and
    reverse deltas to minimize memory usage.

    Pass a pre-built index (e.g. DBDeltaIndex) to skip index construction.
    """
    if index is None:
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
        book.print_book(levels)
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
        elif parts[0].lower() == "d" and len(parts) == 2:
            try:
                levels = parse_levels(parts[1])
                if levels is None:
                    print("Showing all levels")
                else:
                    print(f"Showing {levels} levels")
            except argparse.ArgumentTypeError:
                print("Invalid level count. Use: d <number> or d max")
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


def animate_book(
    deltas_path: Optional[str],
    output_path: Optional[str] = None,
    interval: int = 50,
    step: int = 1,
    tower_levels: int = 15,
    index: Optional[DeltaIndex | DBDeltaIndex] = None,
) -> None:
    """
    Animate the order book depth and tower charts over time.

    Pre-renders all frames into lightweight data snapshots in a single O(N)
    forward pass, then drives FuncAnimation from that list — no delta replay
    or heavy computation during drawing.

    Args:
        deltas_path: Path to deltas.csv (unused when index is provided).
        output_path: Save to file (mp4/gif) instead of showing interactively.
        interval: Milliseconds between frames.
        step: Sample every Nth timestamp (higher = fewer frames, faster export).
        tower_levels: Max price levels shown per side in the tower chart.
        index: Pre-built index (e.g. DBDeltaIndex); built from deltas_path if None.
    """
    sns.set_theme(style="darkgrid")
    bid_color = sns.color_palette("deep")[2]
    ask_color = sns.color_palette("deep")[3]

    if index is None:
        print("Building index...")
        index = DeltaIndex(deltas_path)
    if len(index) == 0:
        print("No deltas found in file.")
        return

    # --- Pre-render phase: single O(N) forward pass ---
    n_ts = len(index)
    print(f"Pre-rendering {n_ts} timestamps (sampling every {step})...")
    frames: list[_FrameData] = []
    book = OrderBook()
    report_every = max(1, n_ts // 20)
    for i in range(n_ts):
        for delta in index.read_deltas_at_index(i):
            book.apply_delta(delta)
        if i % step == 0 or i == n_ts - 1:
            frames.append(_build_frame(book, index.timestamps[i], tower_levels))
        if (i + 1) % report_every == 0:
            print(f"  {100 * (i + 1) // n_ts}%", end="\r", flush=True)
    del book  # no longer needed; only the frame snapshots remain in memory
    print(f"Pre-rendered {len(frames)} frames.          ")

    # --- Render phase: pure matplotlib, zero computation per frame ---
    n_frames = len(frames)
    fig, (ax_depth, ax_tower) = plt.subplots(1, 2, figsize=(18, 7))
    title = fig.suptitle("", fontsize=13)
    plt.tight_layout(rect=(0, 0, 1, 0.95))

    def update(frame_idx: int) -> None:
        fd = frames[frame_idx]

        ax_depth.cla()
        if fd.bid_prices:
            ax_depth.fill_between(fd.bid_prices, fd.bid_cum, step="post", alpha=0.35, color=bid_color)
            ax_depth.step(fd.bid_prices, fd.bid_cum, where="post", color=bid_color, label="Bids", linewidth=1.5)
        if fd.ask_prices:
            ax_depth.fill_between(fd.ask_prices, fd.ask_cum, step="post", alpha=0.35, color=ask_color)
            ax_depth.step(fd.ask_prices, fd.ask_cum, where="post", color=ask_color, label="Asks", linewidth=1.5)
        ax_depth.set_xlabel("Price")
        ax_depth.set_ylabel("Cumulative Quantity")
        ax_depth.set_title("Cumulative Depth")
        ax_depth.legend()

        ax_tower.cla()
        if fd.tower_prices:
            y_pos = list(range(len(fd.tower_prices)))
            ax_tower.barh(y_pos, [-q for q in fd.tower_bid_qtys], height=0.6, color=bid_color, alpha=0.7, label="Bids")
            ax_tower.barh(y_pos, fd.tower_ask_qtys, height=0.6, color=ask_color, alpha=0.7, label="Asks")
            ax_tower.set_yticks(y_pos)
            ax_tower.set_yticklabels(fd.tower_prices, fontsize=8)
            ax_tower.axvline(0, color="white", linewidth=0.8)
            x_max = max(max(fd.tower_bid_qtys, default=0), max(fd.tower_ask_qtys, default=0), 1)
            ax_tower.set_xlim(-x_max * 1.1, x_max * 1.1)
            ax_tower.xaxis.set_major_formatter(mticker.FuncFormatter(lambda x, _: str(int(abs(x)))))
            ax_tower.legend()
        ax_tower.set_xlabel("Quantity")
        ax_tower.set_ylabel("Price")
        ax_tower.set_title("Quantity at Each Level")

        title.set_text(f"Order Book — Timestamp {fd.timestamp}  [{frame_idx + 1}/{n_frames}]")

    anim = animation.FuncAnimation(fig, update, frames=n_frames, interval=interval, repeat=False)

    if output_path:
        print(f"Saving to {output_path} ...")
        fps = max(1, 1000 // interval)
        try:
            anim.save(output_path, writer=animation.FFMpegWriter(fps=fps))
        except FileNotFoundError:
            print("ffmpeg not found, falling back to PillowWriter (slower for large files)")
            anim.save(output_path, writer=animation.PillowWriter(fps=fps))
        print(f"Saved to {output_path}")
    else:
        plt.show()


def parse_levels(value: str) -> int | None:
    """Parse the --levels argument, accepting either an integer or 'max'."""
    if value.lower() == "max":
        return None
    try:
        return int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            f"'{value}' is not a valid level count (exception={exc}) (use an integer or 'max')"
        )


def main() -> None:
    """Entry point for the order book visualizer CLI."""
    parser = argparse.ArgumentParser(
        description="Visualize order book from market-sim deltas.csv or a PostgreSQL run"
    )
    parser.add_argument(
        "deltas_file",
        nargs="?",
        help="Path to deltas.csv file (omit when using --run-id)",
    )
    parser.add_argument(
        "--run-id",
        metavar="UUID",
        help="PostgreSQL run_id to read deltas from (requires --conn)",
    )
    parser.add_argument(
        "--conn",
        metavar="DSN",
        default="postgresql://localhost:5433/market_sim?host=/tmp",
        help="PostgreSQL connection string (default: %(default)s)",
    )
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
        type=parse_levels,
        default=10,
        help="Number of price levels to show (default: 10, use 'max' for all)",
    )
    parser.add_argument(
        "--animate",
        action="store_true",
        help="Animate the order book over time",
    )
    parser.add_argument(
        "--animate-output",
        metavar="PATH",
        help="Save animation to file (e.g. book.mp4 or book.gif) instead of showing",
    )
    parser.add_argument(
        "--animate-interval",
        type=int,
        default=50,
        metavar="MS",
        help="Milliseconds between animation frames (default: 50)",
    )
    parser.add_argument(
        "--animate-step",
        type=int,
        default=1,
        metavar="N",
        help="Sample every Nth timestamp (default: 1)",
    )
    parser.add_argument(
        "--animate-levels",
        type=int,
        default=15,
        metavar="N",
        help="Max price levels per side shown in the tower chart (default: 15)",
    )

    args = parser.parse_args()

    # Validate source selection
    use_db = args.run_id is not None
    if not use_db and not args.deltas_file:
        parser.error("Provide either a deltas_file or --run-id (with --conn)")

    # Build index once; reused across all modes
    if use_db:
        index: DeltaIndex | DBDeltaIndex = DBDeltaIndex(args.run_id, args.conn)
    else:
        index = None  # built lazily inside each function for file mode

    if args.animate or args.animate_output:
        animate_book(
            args.deltas_file,
            output_path=args.animate_output,
            interval=args.animate_interval,
            step=args.animate_step,
            tower_levels=args.animate_levels,
            index=index,
        )
    elif args.interactive:
        interactive_mode(args.deltas_file, args.levels, index=index)
    elif args.at is not None:
        if use_db:
            book = reconstruct_at_from_index(index, args.at)
        else:
            book = reconstruct_at(args.deltas_file, args.at)
        book.print_book(args.levels)
        if args.plot or args.plot_output:
            plot_depth(book, args.plot_output)
    else:
        if use_db:
            if len(index) == 0:
                print("No deltas found for this run.")
                return
            book = reconstruct_at_from_index(index, index.timestamps[-1])
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
