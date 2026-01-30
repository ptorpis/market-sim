"""
Cross-validation tests for market simulator.

These tests verify that the Python replay engine produces exactly
the same state as the C++ simulation engine.
"""

from tools.visualizer.order_book import OrderBook, Order, Side
from tools.testing.state_comparator import StateComparator
from tools.testing.pnl_tracker import PnLTracker
from tools.testing.cross_validator import CrossValidator


class TestStateComparator:
    """Tests for the StateComparator class."""

    def test_compare_empty_books_match(self):
        """Empty order books should match."""
        comparator = StateComparator()
        cpp_book = {"bids": [], "asks": []}
        py_book = OrderBook()

        diffs = comparator.compare_order_books(cpp_book, py_book, instrument_id=1)
        assert diffs == []

    def test_compare_single_order_match(self, sample_state_json):
        """Single order in book should match when state is correct."""
        comparator = StateComparator()

        # Build Python book to match the sample state
        py_book = OrderBook()
        order = Order(
            order_id=1,
            client_id=100,
            side=Side.BUY,
            price=1000,
            quantity=50,
            timestamp=100,
        )
        py_book._add_order(order)

        cpp_book = sample_state_json["order_books"]["1"]
        diffs = comparator.compare_order_books(cpp_book, py_book, instrument_id=1)
        assert diffs == []

    def test_compare_order_quantity_mismatch(self, sample_state_json):
        """Detect quantity mismatch between C++ and Python order."""
        comparator = StateComparator()

        # Build Python book with wrong quantity
        py_book = OrderBook()
        order = Order(
            order_id=1,
            client_id=100,
            side=Side.BUY,
            price=1000,
            quantity=25,  # Wrong quantity
            timestamp=100,
        )
        py_book._add_order(order)

        cpp_book = sample_state_json["order_books"]["1"]
        diffs = comparator.compare_order_books(cpp_book, py_book, instrument_id=1)

        assert len(diffs) == 1
        assert "quantity" in diffs[0]
        assert "C++=50" in diffs[0]
        assert "Py=25" in diffs[0]

    def test_compare_missing_order(self, sample_state_json):
        """Detect missing order in Python book."""
        comparator = StateComparator()

        # Empty Python book
        py_book = OrderBook()

        cpp_book = sample_state_json["order_books"]["1"]
        diffs = comparator.compare_order_books(cpp_book, py_book, instrument_id=1)

        assert len(diffs) >= 1
        assert "level count" in diffs[0]

    def test_compare_pnl_match(self, sample_state_with_trade):
        """P&L should match when state is correct."""
        comparator = StateComparator()

        cpp_pnl = sample_state_with_trade["pnl"]
        py_pnl = {
            100: {"long_position": 50, "short_position": 0, "cash": -50000},
            101: {"long_position": 0, "short_position": 50, "cash": 50000},
        }

        diffs = comparator.compare_pnl(cpp_pnl, py_pnl)
        assert diffs == []

    def test_compare_pnl_cash_mismatch(self, sample_state_with_trade):
        """Detect cash mismatch in P&L."""
        comparator = StateComparator()

        cpp_pnl = sample_state_with_trade["pnl"]
        py_pnl = {
            100: {"long_position": 50, "short_position": 0, "cash": -49000},  # Wrong
            101: {"long_position": 0, "short_position": 50, "cash": 50000},
        }

        diffs = comparator.compare_pnl(cpp_pnl, py_pnl)

        assert len(diffs) == 1
        assert "PnL[100].cash" in diffs[0]

    def test_compare_full_state(self, sample_state_json):
        """Full state comparison should work."""
        comparator = StateComparator()

        py_books = {1: OrderBook()}
        order = Order(
            order_id=1,
            client_id=100,
            side=Side.BUY,
            price=1000,
            quantity=50,
            timestamp=100,
        )
        py_books[1]._add_order(order)

        result = comparator.compare_full_state(sample_state_json, py_books, {})

        assert result.match
        assert result.sequence_num == 1
        assert result.timestamp == 100


class TestPnLTracker:
    """Tests for the PnLTracker class."""

    def test_single_trade_updates_both_parties(self):
        """Single trade should update buyer and seller correctly."""
        tracker = PnLTracker()
        tracker.on_trade(buyer_id=1, seller_id=2, price=1000, quantity=50)

        state = tracker.get_state()

        # Buyer gets long position, pays cash
        assert state[1]["long_position"] == 50
        assert state[1]["short_position"] == 0
        assert state[1]["cash"] == -50000

        # Seller gets short position, receives cash
        assert state[2]["long_position"] == 0
        assert state[2]["short_position"] == 50
        assert state[2]["cash"] == 50000

    def test_cash_sums_to_zero(self):
        """Total cash across all participants should sum to zero."""
        tracker = PnLTracker()

        # Multiple trades
        tracker.on_trade(buyer_id=1, seller_id=2, price=1000, quantity=50)
        tracker.on_trade(buyer_id=3, seller_id=1, price=1001, quantity=25)
        tracker.on_trade(buyer_id=2, seller_id=3, price=999, quantity=10)

        assert tracker.total_cash() == 0

    def test_multiple_trades_same_participants(self):
        """Multiple trades between same participants accumulate."""
        tracker = PnLTracker()

        tracker.on_trade(buyer_id=1, seller_id=2, price=1000, quantity=50)
        tracker.on_trade(buyer_id=1, seller_id=2, price=1001, quantity=25)

        state = tracker.get_state()

        assert state[1]["long_position"] == 75
        assert state[1]["cash"] == -(50 * 1000 + 25 * 1001)

        assert state[2]["short_position"] == 75
        assert state[2]["cash"] == 50 * 1000 + 25 * 1001


class TestOrderBookDeltaReplay:
    """Tests for OrderBook delta replay matching C++ behavior."""

    def test_add_delta_creates_order(self):
        """ADD delta should create an order in the book."""
        book = OrderBook()

        delta = {
            "timestamp": "100",
            "delta_type": "ADD",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "50",
            "remaining_qty": "50",
        }

        book.apply_delta(delta)

        order = book.get_order(1)
        assert order is not None
        assert order.order_id == 1
        assert order.client_id == 100
        assert order.price == 1000
        assert order.quantity == 50
        assert order.side == Side.BUY

    def test_fill_delta_partial(self):
        """FILL delta with remaining > 0 should reduce quantity."""
        book = OrderBook()

        add_delta = {
            "timestamp": "100",
            "delta_type": "ADD",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "50",
            "remaining_qty": "50",
        }
        book.apply_delta(add_delta)

        # Partial fill
        fill_delta = {
            "timestamp": "200",
            "delta_type": "FILL",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "20",
            "remaining_qty": "30",
        }
        book.apply_delta(fill_delta)

        order = book.get_order(1)
        assert order is not None
        assert order.quantity == 30

    def test_fill_delta_complete(self):
        """FILL delta with remaining = 0 should remove order."""
        book = OrderBook()

        add_delta = {
            "timestamp": "100",
            "delta_type": "ADD",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "50",
            "remaining_qty": "50",
        }
        book.apply_delta(add_delta)

        # Complete fill
        fill_delta = {
            "timestamp": "200",
            "delta_type": "FILL",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "50",
            "remaining_qty": "0",
        }
        book.apply_delta(fill_delta)

        order = book.get_order(1)
        assert order is None

    def test_cancel_delta_removes_order(self):
        """CANCEL delta should remove order from book."""
        book = OrderBook()

        # Add order
        add_delta = {
            "timestamp": "100",
            "delta_type": "ADD",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "50",
            "remaining_qty": "50",
        }
        book.apply_delta(add_delta)

        # Cancel
        cancel_delta = {
            "timestamp": "200",
            "delta_type": "CANCEL",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "50",
            "remaining_qty": "50",
        }
        book.apply_delta(cancel_delta)

        order = book.get_order(1)
        assert order is None

    def test_modify_delta_changes_order(self):
        """MODIFY delta should remove old order and add new one."""
        book = OrderBook()

        # Add order
        add_delta = {
            "timestamp": "100",
            "delta_type": "ADD",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "50",
            "remaining_qty": "50",
        }
        book.apply_delta(add_delta)

        # Modify (price change = new order_id)
        modify_delta = {
            "timestamp": "200",
            "delta_type": "MODIFY",
            "order_id": "1",
            "client_id": "100",
            "side": "BUY",
            "price": "1000",
            "quantity": "50",
            "remaining_qty": "50",
            "new_order_id": "2",
            "new_price": "1001",
            "new_quantity": "50",
        }
        book.apply_delta(modify_delta)

        # Old order should be gone
        assert book.get_order(1) is None

        # New order should exist
        new_order = book.get_order(2)
        assert new_order is not None
        assert new_order.price == 1001


class TestFIFOOrderPreservation:
    """Tests for FIFO order preservation in price queues."""

    def test_fifo_order_at_same_price(self):
        """Orders at same price should maintain FIFO order."""
        book = OrderBook()

        # Add three orders at same price
        for i in range(1, 4):
            delta = {
                "timestamp": str(100 * i),
                "delta_type": "ADD",
                "order_id": str(i),
                "client_id": str(100 + i),
                "side": "BUY",
                "price": "1000",
                "quantity": "50",
                "remaining_qty": "50",
            }
            book.apply_delta(delta)

        # Check FIFO order
        orders = book.get_orders_at_price(Side.BUY, 1000)
        assert len(orders) == 3
        assert orders[0].order_id == 1  # First in
        assert orders[1].order_id == 2
        assert orders[2].order_id == 3  # Last in


class TestCrossValidatorFileBasedIntegration:
    """
    Integration tests using file-based input.

    These tests create sample CSV files and validate the full pipeline.
    """

    def test_validator_with_sample_data(
        self, temp_test_dir, sample_deltas_content, sample_trades_content
    ):
        """Test validator with sample delta and trade files."""
        # Write sample files
        (temp_test_dir / "deltas.csv").write_text(sample_deltas_content)
        (temp_test_dir / "trades.csv").write_text(sample_trades_content)

        # Create states directory with expected states
        states_dir = temp_test_dir / "states"
        states_dir.mkdir()

        # State 0: empty book
        import json

        state_0 = {
            "timestamp": 0,
            "sequence_num": 0,
            "order_books": {"1": {"bids": [], "asks": []}},
            "pnl": {},
        }
        (states_dir / "state_000000.json").write_text(json.dumps(state_0))

        # State 1: after first ADD (buy order)
        state_1 = {
            "timestamp": 100,
            "sequence_num": 1,
            "order_books": {
                "1": {
                    "bids": [
                        {
                            "price": 1000,
                            "orders": [
                                {
                                    "order_id": 1,
                                    "client_id": 100,
                                    "quantity": 50,
                                    "price": 1000,
                                    "timestamp": 100,
                                    "side": "BUY",
                                }
                            ],
                        }
                    ],
                    "asks": [],
                }
            },
            "pnl": {},
        }
        (states_dir / "state_000001.json").write_text(json.dumps(state_1))

        # State 2: after trade (both orders filled, book empty)
        state_2 = {
            "timestamp": 200,
            "sequence_num": 2,
            "order_books": {"1": {"bids": [], "asks": []}},
            "pnl": {
                "100": {"long_position": 50, "short_position": 0, "cash": -50000},
                "101": {"long_position": 0, "short_position": 50, "cash": 50000},
            },
        }
        (states_dir / "state_000002.json").write_text(json.dumps(state_2))

        # Run validator
        validator = CrossValidator(output_dir=temp_test_dir)
        results = list(validator.validate_all())

        # Check results
        assert len(results) >= 1
        # First state (empty) should match
        assert results[0].match, f"State 0 mismatch: {results[0].differences}"


class TestPnLConservation:
    """Tests for P&L conservation invariants."""

    def test_cash_is_zero_sum(self):
        """In a closed system, cash should sum to zero."""
        tracker = PnLTracker()

        # Simulate several trades
        trades = [
            (1, 2, 1000, 100),  # buyer_id, seller_id, price, qty
            (3, 1, 1001, 50),
            (2, 3, 999, 25),
            (1, 3, 1000, 75),
        ]

        for buyer, seller, price, qty in trades:
            tracker.on_trade(buyer, seller, price, qty)

        # Cash must sum to zero
        assert tracker.total_cash() == 0, "Cash is not zero-sum"

    def test_positions_are_zero_sum(self):
        """Net positions across all participants should sum to zero."""
        tracker = PnLTracker()

        trades = [
            (1, 2, 1000, 100),
            (3, 1, 1001, 50),
            (2, 3, 999, 25),
        ]

        for buyer, seller, price, qty in trades:
            tracker.on_trade(buyer, seller, price, qty)

        # Net positions must sum to zero
        assert tracker.total_net_position() == 0, "Net positions are not zero-sum"
