"""
State comparison for cross-validation testing.

Compares C++ exported state (JSON) with Python OrderBook state to verify
that both engines produce identical results.
"""

from dataclasses import dataclass, field

from tools.visualizer.order_book import OrderBook, Order, Side


@dataclass
class ComparisonResult:
    """Result of comparing C++ and Python states."""

    match: bool
    sequence_num: int
    timestamp: int
    differences: list[str] = field(default_factory=list)

    def __str__(self) -> str:
        if self.match:
            return f"[OK] seq={self.sequence_num} ts={self.timestamp}"
        diff_summary = "; ".join(self.differences[:3])
        if len(self.differences) > 3:
            diff_summary += f" (+{len(self.differences) - 3} more)"
        return f"[FAIL] seq={self.sequence_num} ts={self.timestamp}: {diff_summary}"

    def __repr__(self) -> str:
        return self.__str__()


class StateComparator:
    """
    Compares C++ exported state with Python reconstructed state.

    Validates:
    - Order book structure (bid/ask price levels)
    - Order metadata (order_id, client_id, quantity, price, timestamp, side)
    - FIFO queue order within each price level
    - P&L state for all participants
    """

    def __init__(self, tolerance: int = 0):
        """
        Args:
            tolerance: Allowed numeric tolerance for comparisons (default 0 for exact match)
        """
        self.tolerance = tolerance

    def compare_order_books(
        self, cpp_book: dict, py_book: OrderBook, instrument_id: int
    ) -> list[str]:
        """
        Compare order book state between C++ export and Python OrderBook.

        Args:
            cpp_book: JSON dict from C++ state export for one instrument
            py_book: Python OrderBook instance
            instrument_id: Instrument ID for error messages

        Returns:
            List of differences (empty if books match)
        """
        differences = []

        # Compare bids
        cpp_bids = cpp_book.get("bids", [])
        differences.extend(
            self._compare_side(cpp_bids, py_book.bids, Side.BUY, instrument_id)
        )

        # Compare asks
        cpp_asks = cpp_book.get("asks", [])
        differences.extend(
            self._compare_side(cpp_asks, py_book.asks, Side.SELL, instrument_id)
        )

        return differences

    def _compare_side(
        self,
        cpp_levels: list[dict],
        py_book_side: dict,
        side: Side,
        instrument_id: int,
    ) -> list[str]:
        """Compare one side of the order book (bids or asks)."""
        differences = []
        side_name = "bid" if side == Side.BUY else "ask"

        # Get Python price levels in correct order
        py_prices = list(py_book_side.keys())

        if len(cpp_levels) != len(py_prices):
            differences.append(
                f"inst={instrument_id} {side_name} level count: "
                f"C++={len(cpp_levels)}, Py={len(py_prices)}"
            )

        # Compare level by level
        for i, cpp_level in enumerate(cpp_levels):
            cpp_price = cpp_level["price"]
            cpp_orders = cpp_level["orders"]

            if i >= len(py_prices):
                differences.append(
                    f"inst={instrument_id} {side_name} extra C++ level at price {cpp_price}"
                )
                continue

            py_price = py_prices[i]

            if cpp_price != py_price:
                differences.append(
                    f"inst={instrument_id} {side_name} level {i} price: "
                    f"C++={cpp_price}, Py={py_price}"
                )
                continue

            # Get Python orders at this price level
            py_orders = list(py_book_side[py_price])

            if len(cpp_orders) != len(py_orders):
                differences.append(
                    f"inst={instrument_id} {side_name}[{cpp_price}] queue length: "
                    f"C++={len(cpp_orders)}, Py={len(py_orders)}"
                )
                continue

            # Compare order by order (FIFO order matters!)
            for j, (cpp_order, py_order) in enumerate(zip(cpp_orders, py_orders)):
                order_diffs = self._compare_orders(
                    cpp_order,
                    py_order,
                    f"inst={instrument_id} {side_name}[{cpp_price}][{j}]",
                )
                differences.extend(order_diffs)

        # Check for extra Python levels
        for i in range(len(cpp_levels), len(py_prices)):
            py_price = py_prices[i]
            differences.append(
                f"inst={instrument_id} {side_name} extra Py level at price {py_price}"
            )

        return differences

    def _compare_orders(
        self, cpp_order: dict, py_order: Order, context: str
    ) -> list[str]:
        """Compare individual order fields."""
        diffs = []

        # Map C++ JSON field names to Python Order attribute names
        fields = [
            ("order_id", cpp_order.get("order_id"), py_order.order_id),
            ("client_id", cpp_order.get("client_id"), py_order.client_id),
            ("quantity", cpp_order.get("quantity"), py_order.quantity),
            ("price", cpp_order.get("price"), py_order.price),
        ]

        for field_name, cpp_val, py_val in fields:
            if cpp_val != py_val:
                diffs.append(f"{context}.{field_name}: C++={cpp_val}, Py={py_val}")

        # Compare side
        cpp_side = cpp_order.get("side")
        py_side = py_order.side.value
        if cpp_side != py_side:
            diffs.append(f"{context}.side: C++={cpp_side}, Py={py_side}")

        return diffs

    def compare_pnl(self, cpp_pnl: dict, py_pnl: dict) -> list[str]:
        """
        Compare P&L state between C++ export and Python tracker.

        Args:
            cpp_pnl: Dict from C++ state, maps str(client_id) -> PnL dict
            py_pnl: Dict from Python tracker, maps int(client_id) -> PnL dict

        Returns:
            List of differences (empty if P&L matches)
        """
        differences = []

        # Normalize keys to int for comparison
        cpp_clients = set(int(k) for k in cpp_pnl.keys())
        py_clients = set(py_pnl.keys())

        only_cpp = cpp_clients - py_clients
        only_py = py_clients - cpp_clients

        if only_cpp:
            differences.append(f"PnL clients only in C++: {only_cpp}")
        if only_py:
            differences.append(f"PnL clients only in Py: {only_py}")

        for client_id in cpp_clients & py_clients:
            cpp_client_pnl = cpp_pnl[str(client_id)]
            py_client_pnl = py_pnl[client_id]

            fields = ["long_position", "short_position", "cash"]
            for field in fields:
                cpp_val = cpp_client_pnl.get(field, 0)
                py_val = py_client_pnl.get(field, 0)

                if abs(cpp_val - py_val) > self.tolerance:
                    differences.append(
                        f"PnL[{client_id}].{field}: C++={cpp_val}, Py={py_val}"
                    )

        return differences

    def compare_full_state(
        self,
        cpp_state: dict,
        py_books: dict[int, OrderBook],
        py_pnl: dict,
    ) -> ComparisonResult:
        """
        Compare complete simulation state.

        Args:
            cpp_state: Full JSON state dict from C++ export
            py_books: Dict mapping instrument_id (int) -> Python OrderBook
            py_pnl: Dict mapping client_id (int) -> PnL dict

        Returns:
            ComparisonResult with match status and any differences
        """
        all_diffs = []

        # Compare order books for each instrument
        cpp_books = cpp_state.get("order_books", {})

        cpp_instruments = set(int(k) for k in cpp_books.keys())
        py_instruments = set(py_books.keys())

        only_cpp = cpp_instruments - py_instruments
        only_py = py_instruments - cpp_instruments

        if only_cpp:
            all_diffs.append(f"Order books only in C++: {only_cpp}")
        if only_py:
            all_diffs.append(f"Order books only in Py: {only_py}")

        # Compare each instrument's order book
        for inst_id in cpp_instruments & py_instruments:
            cpp_book = cpp_books[str(inst_id)]
            py_book = py_books[inst_id]

            book_diffs = self.compare_order_books(cpp_book, py_book, inst_id)
            all_diffs.extend(book_diffs)

        # Compare P&L
        cpp_pnl = cpp_state.get("pnl", {})
        pnl_diffs = self.compare_pnl(cpp_pnl, py_pnl)
        all_diffs.extend(pnl_diffs)

        return ComparisonResult(
            match=len(all_diffs) == 0,
            sequence_num=cpp_state.get("sequence_num", -1),
            timestamp=cpp_state.get("timestamp", -1),
            differences=all_diffs,
        )
