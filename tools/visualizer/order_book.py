from dataclasses import dataclass
from enum import Enum
from collections import deque
from typing import Optional

from sortedcontainers import SortedDict


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
                    order = Order(
                        order_id, client_id, side, price, prev_quantity, orig_ts
                    )
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

    def get_orders_at_price(self, side: Side, price: int = -1) -> list[Order]:
        book = self._get_book(side)
        if price == -1:
            print(book.peekitem(0))

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
