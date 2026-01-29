"""
Tests for visualize_book.py order book visualization tool.

Tests cover:
- OrderBook basic operations (add, remove, update orders)
- Forward delta application (ADD, FILL, CANCEL, MODIFY)
- Reverse delta application for backward navigation
- Forward/backward scan consistency (critical invariant)
- DeltaIndex file indexing
- Helper functions (best_bid, best_ask, spread, midpoint, depth)
"""

import os
import tempfile

import pytest

from tools.visualize_book import (
    DeltaIndex,
    Order,
    OrderBook,
    Side,
    read_deltas,
    reconstruct_at,
)


# =============================================================================
# Fixtures
# =============================================================================


@pytest.fixture
def empty_book():
    """Return an empty OrderBook."""
    return OrderBook()


@pytest.fixture
def sample_order():
    """Return a sample buy order."""
    return Order(
        order_id=1,
        client_id=100,
        side=Side.BUY,
        price=1000,
        quantity=50,
        timestamp=0,
    )


@pytest.fixture
def sample_deltas_file():
    """Create a temporary deltas file with sample data."""
    content = """timestamp,sequence_num,delta_type,order_id,client_id,instrument_id,side,price,quantity,remaining_qty,trade_id,new_order_id,new_price,new_quantity
0,0,ADD,1,100,1,BUY,999,100,100,0,0,0,0
0,1,ADD,2,101,1,SELL,1001,100,100,0,0,0,0
10,2,ADD,3,102,1,BUY,998,50,50,0,0,0,0
20,3,FILL,1,100,1,BUY,999,30,70,1,0,0,0
30,4,FILL,1,100,1,BUY,999,70,0,2,0,0,0
40,5,CANCEL,3,102,1,BUY,998,50,50,0,0,0,0
50,6,ADD,4,103,1,SELL,1002,80,80,0,0,0,0
"""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False) as f:
        f.write(content)
        f.flush()
        yield f.name
    os.unlink(f.name)


@pytest.fixture
def modify_deltas_file():
    """Create a deltas file with MODIFY operations."""
    content = """timestamp,sequence_num,delta_type,order_id,client_id,instrument_id,side,price,quantity,remaining_qty,trade_id,new_order_id,new_price,new_quantity
0,0,ADD,1,100,1,BUY,999,100,100,0,0,0,0
10,1,MODIFY,1,100,1,BUY,999,100,0,0,2,1000,80
20,2,ADD,3,101,1,SELL,1005,50,50,0,0,0,0
"""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False) as f:
        f.write(content)
        f.flush()
        yield f.name
    os.unlink(f.name)


@pytest.fixture
def complex_deltas_file():
    """Create a deltas file with multiple operations at the same timestamp."""
    content = """timestamp,sequence_num,delta_type,order_id,client_id,instrument_id,side,price,quantity,remaining_qty,trade_id,new_order_id,new_price,new_quantity
0,0,ADD,1,100,1,BUY,999,100,100,0,0,0,0
0,1,ADD,2,101,1,SELL,1001,100,100,0,0,0,0
10,2,ADD,3,102,1,BUY,998,50,50,0,0,0,0
10,3,ADD,4,103,1,BUY,997,30,30,0,0,0,0
10,4,ADD,5,104,1,SELL,1002,60,60,0,0,0,0
20,5,FILL,1,100,1,BUY,999,40,60,1,0,0,0
20,6,FILL,6,105,1,SELL,999,40,0,1,0,0,0
30,7,CANCEL,3,102,1,BUY,998,50,50,0,0,0,0
30,8,ADD,7,106,1,BUY,999,25,25,0,0,0,0
40,9,MODIFY,4,103,1,BUY,997,30,0,0,8,996,40
50,10,FILL,1,100,1,BUY,999,60,0,2,0,0,0
50,11,FILL,9,107,1,SELL,999,60,0,2,0,0,0
"""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False) as f:
        f.write(content)
        f.flush()
        yield f.name
    os.unlink(f.name)


def make_delta(
    timestamp,
    delta_type,
    order_id,
    client_id,
    side,
    price,
    quantity,
    remaining_qty,
    new_order_id=0,
    new_price=0,
    new_quantity=0,
):
    """Helper to create delta dicts for testing."""
    return {
        "timestamp": str(timestamp),
        "delta_type": delta_type,
        "order_id": str(order_id),
        "client_id": str(client_id),
        "side": side,
        "price": str(price),
        "quantity": str(quantity),
        "remaining_qty": str(remaining_qty),
        "new_order_id": str(new_order_id),
        "new_price": str(new_price),
        "new_quantity": str(new_quantity),
    }


def get_book_state(book):
    """Extract a comparable state representation from an OrderBook."""
    bids = {}
    for price in book.bids.keys():
        orders = [(o.order_id, o.quantity) for o in book.bids[price]]
        bids[price] = orders

    asks = {}
    for price in book.asks.keys():
        orders = [(o.order_id, o.quantity) for o in book.asks[price]]
        asks[price] = orders

    return {
        "timestamp": book.timestamp,
        "bids": bids,
        "asks": asks,
        "registry": dict(book.registry),
    }


def states_equal(state1, state2):
    """Compare two book states for equality."""
    return (
        state1["timestamp"] == state2["timestamp"]
        and state1["bids"] == state2["bids"]
        and state1["asks"] == state2["asks"]
        and state1["registry"] == state2["registry"]
    )


# =============================================================================
# OrderBook Basic Operations Tests
# =============================================================================


class TestOrderBookBasicOperations:
    """Tests for low-level OrderBook operations."""

    def test_empty_book_initialization(self, empty_book):
        """Empty book should have no orders."""
        assert len(empty_book.bids) == 0
        assert len(empty_book.asks) == 0
        assert len(empty_book.registry) == 0
        assert empty_book.timestamp == 0

    def test_add_buy_order(self, empty_book, sample_order):
        """Adding a buy order should place it in bids."""
        empty_book._add_order(sample_order)

        assert sample_order.price in empty_book.bids
        assert len(empty_book.bids[sample_order.price]) == 1
        assert empty_book.registry[sample_order.order_id] == (
            sample_order.price,
            Side.BUY,
        )

    def test_add_sell_order(self, empty_book):
        """Adding a sell order should place it in asks."""
        order = Order(
            order_id=2,
            client_id=101,
            side=Side.SELL,
            price=1010,
            quantity=30,
            timestamp=0,
        )
        empty_book._add_order(order)

        assert order.price in empty_book.asks
        assert len(empty_book.asks[order.price]) == 1
        assert empty_book.registry[order.order_id] == (order.price, Side.SELL)

    def test_add_multiple_orders_same_price(self, empty_book):
        """Multiple orders at same price should form a queue."""
        order1 = Order(1, 100, Side.BUY, 1000, 50, 0)
        order2 = Order(2, 101, Side.BUY, 1000, 30, 1)

        empty_book._add_order(order1)
        empty_book._add_order(order2)

        assert len(empty_book.bids[1000]) == 2
        assert empty_book.bids[1000][0].order_id == 1
        assert empty_book.bids[1000][1].order_id == 2

    def test_remove_order(self, empty_book, sample_order):
        """Removing an order should return it and update book."""
        empty_book._add_order(sample_order)
        removed = empty_book._remove_order(sample_order.order_id)

        assert removed is not None
        assert removed.order_id == sample_order.order_id
        assert sample_order.order_id not in empty_book.registry
        assert sample_order.price not in empty_book.bids

    def test_remove_nonexistent_order(self, empty_book):
        """Removing nonexistent order should return None."""
        result = empty_book._remove_order(999)
        assert result is None

    def test_remove_order_keeps_others_at_price(self, empty_book):
        """Removing one order should keep others at same price level."""
        order1 = Order(1, 100, Side.BUY, 1000, 50, 0)
        order2 = Order(2, 101, Side.BUY, 1000, 30, 1)

        empty_book._add_order(order1)
        empty_book._add_order(order2)
        empty_book._remove_order(1)

        assert 1000 in empty_book.bids
        assert len(empty_book.bids[1000]) == 1
        assert empty_book.bids[1000][0].order_id == 2

    def test_update_order_quantity(self, empty_book, sample_order):
        """Updating order quantity should modify in place."""
        empty_book._add_order(sample_order)
        empty_book._update_order_quantity(sample_order.order_id, 25)

        order = empty_book.get_order(sample_order.order_id)
        assert order.quantity == 25

    def test_update_nonexistent_order(self, empty_book):
        """Updating nonexistent order should do nothing."""
        empty_book._update_order_quantity(999, 50)

    def test_bid_sorting_descending(self, empty_book):
        """Bids should be sorted descending (highest price first)."""
        empty_book._add_order(Order(1, 100, Side.BUY, 1000, 50, 0))
        empty_book._add_order(Order(2, 101, Side.BUY, 1005, 30, 0))
        empty_book._add_order(Order(3, 102, Side.BUY, 995, 40, 0))

        prices = list(empty_book.bids.keys())
        assert prices == [1005, 1000, 995]

    def test_ask_sorting_ascending(self, empty_book):
        """Asks should be sorted ascending (lowest price first)."""
        empty_book._add_order(Order(1, 100, Side.SELL, 1010, 50, 0))
        empty_book._add_order(Order(2, 101, Side.SELL, 1005, 30, 0))
        empty_book._add_order(Order(3, 102, Side.SELL, 1015, 40, 0))

        prices = list(empty_book.asks.keys())
        assert prices == [1005, 1010, 1015]


# =============================================================================
# Apply Delta Tests
# =============================================================================


class TestApplyDelta:
    """Tests for forward delta application."""

    def test_apply_add_delta(self, empty_book):
        """ADD delta should create a new order."""
        delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        empty_book.apply_delta(delta)

        assert empty_book.timestamp == 10
        order = empty_book.get_order(1)
        assert order is not None
        assert order.price == 999
        assert order.quantity == 50
        assert empty_book.order_add_timestamps[1] == 10

    def test_apply_fill_partial(self, empty_book):
        """FILL delta with remaining > 0 should reduce quantity."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        fill_delta = make_delta(20, "FILL", 1, 100, "BUY", 999, 20, 30)

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(fill_delta)

        order = empty_book.get_order(1)
        assert order is not None
        assert order.quantity == 30

    def test_apply_fill_complete(self, empty_book):
        """FILL delta with remaining = 0 should remove order."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        fill_delta = make_delta(20, "FILL", 1, 100, "BUY", 999, 50, 0)

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(fill_delta)

        order = empty_book.get_order(1)
        assert order is None
        assert 1 not in empty_book.registry

    def test_apply_cancel_delta(self, empty_book):
        """CANCEL delta should remove the order."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        cancel_delta = make_delta(20, "CANCEL", 1, 100, "BUY", 999, 50, 50)

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(cancel_delta)

        order = empty_book.get_order(1)
        assert order is None

    def test_apply_modify_delta(self, empty_book):
        """MODIFY delta should replace order with new parameters."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        modify_delta = make_delta(
            20,
            "MODIFY",
            1,
            100,
            "BUY",
            999,
            50,
            0,
            new_order_id=2,
            new_price=1000,
            new_quantity=40,
        )

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(modify_delta)

        old_order = empty_book.get_order(1)
        assert old_order is None

        new_order = empty_book.get_order(2)
        assert new_order is not None
        assert new_order.price == 1000
        assert new_order.quantity == 40
        assert empty_book.order_add_timestamps[2] == 20


# =============================================================================
# Apply Reverse Delta Tests
# =============================================================================


class TestApplyReverseDelta:
    """Tests for backward (reverse) delta application."""

    def test_reverse_add_delta(self, empty_book):
        """Reversing ADD should remove the order."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)

        empty_book.apply_delta(add_delta)
        empty_book.apply_reverse_delta(add_delta, prev_timestamp=0)

        assert empty_book.timestamp == 0
        assert empty_book.get_order(1) is None
        assert 1 not in empty_book.order_add_timestamps

    def test_reverse_fill_partial(self, empty_book):
        """Reversing partial FILL should restore original quantity."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        fill_delta = make_delta(20, "FILL", 1, 100, "BUY", 999, 20, 30)

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(fill_delta)

        empty_book.apply_reverse_delta(fill_delta, prev_timestamp=10)

        order = empty_book.get_order(1)
        assert order is not None
        assert order.quantity == 50

    def test_reverse_fill_complete(self, empty_book):
        """Reversing complete FILL should restore the removed order."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        fill_delta = make_delta(20, "FILL", 1, 100, "BUY", 999, 50, 0)

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(fill_delta)

        empty_book.apply_reverse_delta(fill_delta, prev_timestamp=10)

        order = empty_book.get_order(1)
        assert order is not None
        assert order.quantity == 50
        assert order.timestamp == 10

    def test_reverse_cancel_delta(self, empty_book):
        """Reversing CANCEL should restore the order."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        cancel_delta = make_delta(20, "CANCEL", 1, 100, "BUY", 999, 50, 50)

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(cancel_delta)

        empty_book.apply_reverse_delta(cancel_delta, prev_timestamp=10)

        order = empty_book.get_order(1)
        assert order is not None
        assert order.quantity == 50

    def test_reverse_modify_delta(self, empty_book):
        """Reversing MODIFY should restore original order and remove new one."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        modify_delta = make_delta(
            20,
            "MODIFY",
            1,
            100,
            "BUY",
            999,
            50,
            0,
            new_order_id=2,
            new_price=1000,
            new_quantity=40,
        )

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(modify_delta)

        empty_book.apply_reverse_delta(modify_delta, prev_timestamp=10)

        new_order = empty_book.get_order(2)
        assert new_order is None

        old_order = empty_book.get_order(1)
        assert old_order is not None
        assert old_order.price == 999
        assert old_order.quantity == 50


# =============================================================================
# Forward/Backward Consistency Tests (Critical)
# =============================================================================


class TestForwardBackwardConsistency:
    """
    Tests verifying that stepping forward then backward returns to the same state.

    This is the critical invariant that the user reported was failing.
    """

    def test_single_add_forward_backward(self, empty_book):
        """Single ADD: forward then backward should return to empty state."""
        initial_state = get_book_state(empty_book)

        delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        empty_book.apply_delta(delta)
        empty_book.apply_reverse_delta(delta, prev_timestamp=0)

        final_state = get_book_state(empty_book)
        assert states_equal(initial_state, final_state)

    def test_add_fill_forward_backward(self, empty_book):
        """ADD + FILL: forward then backward should restore state at each step."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        fill_delta = make_delta(20, "FILL", 1, 100, "BUY", 999, 20, 30)

        empty_book.apply_delta(add_delta)
        state_after_add = get_book_state(empty_book)

        empty_book.apply_delta(fill_delta)

        empty_book.apply_reverse_delta(fill_delta, prev_timestamp=10)
        state_reversed_to_add = get_book_state(empty_book)

        assert states_equal(state_after_add, state_reversed_to_add)

    def test_multiple_deltas_full_cycle(self, empty_book):
        """Multiple deltas: full forward then full backward cycle."""
        deltas = [
            make_delta(10, "ADD", 1, 100, "BUY", 999, 100, 100),
            make_delta(10, "ADD", 2, 101, "SELL", 1001, 100, 100),
            make_delta(20, "ADD", 3, 102, "BUY", 998, 50, 50),
            make_delta(30, "FILL", 1, 100, "BUY", 999, 30, 70),
            make_delta(40, "CANCEL", 3, 102, "BUY", 998, 50, 50),
        ]

        states = [get_book_state(empty_book)]

        for delta in deltas:
            empty_book.apply_delta(delta)
            states.append(get_book_state(empty_book))

        timestamps = [0, 10, 10, 20, 30, 40]

        for i in range(len(deltas) - 1, -1, -1):
            empty_book.apply_reverse_delta(deltas[i], prev_timestamp=timestamps[i])
            expected_state = states[i]
            actual_state = get_book_state(empty_book)
            assert states_equal(
                expected_state, actual_state
            ), f"Mismatch after reversing delta {i}"

    def test_file_based_forward_backward_consistency(self, sample_deltas_file):
        """Test forward/backward consistency using file-based deltas."""
        index = DeltaIndex(sample_deltas_file)
        book = OrderBook()

        all_delta_groups = []
        all_states = [get_book_state(book)]

        for i in range(len(index)):
            deltas = index.read_deltas_at_index(i)
            all_delta_groups.append(deltas)
            for delta in deltas:
                book.apply_delta(delta)
            all_states.append(get_book_state(book))

        for i in range(len(index) - 1, -1, -1):
            prev_ts = index.timestamps[i - 1] if i > 0 else 0
            for delta in reversed(all_delta_groups[i]):
                book.apply_reverse_delta(delta, prev_timestamp=prev_ts)

            expected = all_states[i]
            actual = get_book_state(book)
            assert states_equal(expected, actual), f"Mismatch at index {i}"

    def test_modify_forward_backward(self, modify_deltas_file):
        """Test forward/backward consistency with MODIFY operations."""
        index = DeltaIndex(modify_deltas_file)
        book = OrderBook()

        all_delta_groups = []
        all_states = [get_book_state(book)]

        for i in range(len(index)):
            deltas = index.read_deltas_at_index(i)
            all_delta_groups.append(deltas)
            for delta in deltas:
                book.apply_delta(delta)
            all_states.append(get_book_state(book))

        for i in range(len(index) - 1, -1, -1):
            prev_ts = index.timestamps[i - 1] if i > 0 else 0
            for delta in reversed(all_delta_groups[i]):
                book.apply_reverse_delta(delta, prev_timestamp=prev_ts)

            expected = all_states[i]
            actual = get_book_state(book)
            assert states_equal(expected, actual), f"Mismatch at index {i}"

    def test_complex_multi_delta_timestamps(self, complex_deltas_file):
        """Test with multiple deltas at same timestamp (realistic scenario)."""
        index = DeltaIndex(complex_deltas_file)
        book = OrderBook()

        all_delta_groups = []
        all_states = [get_book_state(book)]

        for i in range(len(index)):
            deltas = index.read_deltas_at_index(i)
            all_delta_groups.append(deltas)
            for delta in deltas:
                book.apply_delta(delta)
            all_states.append(get_book_state(book))

        for i in range(len(index) - 1, -1, -1):
            prev_ts = index.timestamps[i - 1] if i > 0 else 0
            for delta in reversed(all_delta_groups[i]):
                book.apply_reverse_delta(delta, prev_timestamp=prev_ts)

            expected = all_states[i]
            actual = get_book_state(book)
            assert states_equal(expected, actual), f"Mismatch at index {i}"

    def test_step_forward_backward_alternating(self, sample_deltas_file):
        """Test alternating forward/backward steps like in interactive mode."""
        index = DeltaIndex(sample_deltas_file)

        book = OrderBook()
        deltas_0 = index.read_deltas_at_index(0)
        for delta in deltas_0:
            book.apply_delta(delta)

        state_at_0 = get_book_state(book)

        deltas_1 = index.read_deltas_at_index(1)
        for delta in deltas_1:
            book.apply_delta(delta)
        state_at_1 = get_book_state(book)

        prev_ts = index.timestamps[0]
        for delta in reversed(deltas_1):
            book.apply_reverse_delta(delta, prev_timestamp=prev_ts)

        assert states_equal(state_at_0, get_book_state(book))

        for delta in deltas_1:
            book.apply_delta(delta)
        assert states_equal(state_at_1, get_book_state(book))

        prev_ts = index.timestamps[0]
        for delta in reversed(deltas_1):
            book.apply_reverse_delta(delta, prev_timestamp=prev_ts)
        assert states_equal(state_at_0, get_book_state(book))


# =============================================================================
# DeltaIndex Tests
# =============================================================================


class TestDeltaIndex:
    """Tests for the DeltaIndex file indexer."""

    def test_index_builds_correctly(self, sample_deltas_file):
        """Index should identify all unique timestamps."""
        index = DeltaIndex(sample_deltas_file)

        assert len(index) == 6
        assert index.timestamps == [0, 10, 20, 30, 40, 50]

    def test_read_deltas_at_index(self, sample_deltas_file):
        """Reading at an index should return correct deltas."""
        index = DeltaIndex(sample_deltas_file)

        deltas_at_0 = index.read_deltas_at_index(0)
        assert len(deltas_at_0) == 2
        assert all(d["timestamp"] == "0" for d in deltas_at_0)

        deltas_at_1 = index.read_deltas_at_index(1)
        assert len(deltas_at_1) == 1
        assert deltas_at_1[0]["delta_type"] == "ADD"
        assert deltas_at_1[0]["order_id"] == "3"

    def test_read_deltas_out_of_bounds(self, sample_deltas_file):
        """Reading out of bounds should return empty list."""
        index = DeltaIndex(sample_deltas_file)

        assert index.read_deltas_at_index(-1) == []
        assert index.read_deltas_at_index(100) == []

    def test_read_deltas_up_to_index(self, sample_deltas_file):
        """Reading up to index should yield all deltas up to that timestamp."""
        index = DeltaIndex(sample_deltas_file)

        deltas = list(index.read_deltas_up_to_index(2))
        assert len(deltas) == 4
        assert deltas[0]["delta_type"] == "ADD"
        assert deltas[-1]["delta_type"] == "FILL"

    def test_find_timestamp_index_exact(self, sample_deltas_file):
        """Finding exact timestamp should return correct index."""
        index = DeltaIndex(sample_deltas_file)

        assert index.find_timestamp_index(0) == 0
        assert index.find_timestamp_index(20) == 2
        assert index.find_timestamp_index(50) == 5

    def test_find_timestamp_index_closest(self, sample_deltas_file):
        """Finding non-existent timestamp should return closest."""
        index = DeltaIndex(sample_deltas_file)

        assert index.find_timestamp_index(15) in [1, 2]
        assert index.find_timestamp_index(25) in [2, 3]


# =============================================================================
# Helper Function Tests
# =============================================================================


class TestHelperFunctions:
    """Tests for OrderBook helper methods."""

    def test_best_bid_empty(self, empty_book):
        """Empty book should have no best bid."""
        assert empty_book.best_bid() is None

    def test_best_bid_single(self, empty_book):
        """Single bid should be the best bid."""
        empty_book._add_order(Order(1, 100, Side.BUY, 1000, 50, 0))
        result = empty_book.best_bid()
        assert result == (1000, 50)

    def test_best_bid_multiple_levels(self, empty_book):
        """Best bid should be highest price level."""
        empty_book._add_order(Order(1, 100, Side.BUY, 1000, 50, 0))
        empty_book._add_order(Order(2, 101, Side.BUY, 1005, 30, 0))
        empty_book._add_order(Order(3, 102, Side.BUY, 995, 40, 0))

        result = empty_book.best_bid()
        assert result == (1005, 30)

    def test_best_bid_aggregates_quantity(self, empty_book):
        """Best bid quantity should aggregate all orders at price."""
        empty_book._add_order(Order(1, 100, Side.BUY, 1000, 50, 0))
        empty_book._add_order(Order(2, 101, Side.BUY, 1000, 30, 0))

        result = empty_book.best_bid()
        assert result == (1000, 80)

    def test_best_ask_empty(self, empty_book):
        """Empty book should have no best ask."""
        assert empty_book.best_ask() is None

    def test_best_ask_single(self, empty_book):
        """Single ask should be the best ask."""
        empty_book._add_order(Order(1, 100, Side.SELL, 1010, 50, 0))
        result = empty_book.best_ask()
        assert result == (1010, 50)

    def test_best_ask_multiple_levels(self, empty_book):
        """Best ask should be lowest price level."""
        empty_book._add_order(Order(1, 100, Side.SELL, 1010, 50, 0))
        empty_book._add_order(Order(2, 101, Side.SELL, 1005, 30, 0))
        empty_book._add_order(Order(3, 102, Side.SELL, 1015, 40, 0))

        result = empty_book.best_ask()
        assert result == (1005, 30)

    def test_spread_calculation(self, empty_book):
        """Spread should be ask price minus bid price."""
        empty_book._add_order(Order(1, 100, Side.BUY, 1000, 50, 0))
        empty_book._add_order(Order(2, 101, Side.SELL, 1010, 50, 0))

        assert empty_book.spread() == 10

    def test_spread_no_bids(self, empty_book):
        """Spread should be None with no bids."""
        empty_book._add_order(Order(1, 100, Side.SELL, 1010, 50, 0))
        assert empty_book.spread() is None

    def test_spread_no_asks(self, empty_book):
        """Spread should be None with no asks."""
        empty_book._add_order(Order(1, 100, Side.BUY, 1000, 50, 0))
        assert empty_book.spread() is None

    def test_midpoint_calculation(self, empty_book):
        """Midpoint should be average of best bid and ask."""
        empty_book._add_order(Order(1, 100, Side.BUY, 1000, 50, 0))
        empty_book._add_order(Order(2, 101, Side.SELL, 1010, 50, 0))

        assert empty_book.midpoint() == 1005.0

    def test_midpoint_empty(self, empty_book):
        """Midpoint should be None for empty book."""
        assert empty_book.midpoint() is None

    def test_get_depth(self, empty_book):
        """get_depth should return specified number of levels."""
        for i in range(5):
            empty_book._add_order(Order(i + 1, 100, Side.BUY, 1000 - i, 10, 0))
            empty_book._add_order(Order(i + 10, 100, Side.SELL, 1010 + i, 10, 0))

        bid_levels, ask_levels = empty_book.get_depth(levels=3)

        assert len(bid_levels) == 3
        assert len(ask_levels) == 3
        assert bid_levels[0][0] == 1000
        assert ask_levels[0][0] == 1010

    def test_get_full_depth(self, empty_book):
        """get_full_depth should return all levels."""
        for i in range(5):
            empty_book._add_order(Order(i + 1, 100, Side.BUY, 1000 - i, 10, 0))
            empty_book._add_order(Order(i + 10, 100, Side.SELL, 1010 + i, 10, 0))

        bid_levels, ask_levels = empty_book.get_full_depth()

        assert len(bid_levels) == 5
        assert len(ask_levels) == 5

    def test_get_orders_at_price(self, empty_book):
        """get_orders_at_price should return all orders at that level."""
        empty_book._add_order(Order(1, 100, Side.BUY, 1000, 50, 0))
        empty_book._add_order(Order(2, 101, Side.BUY, 1000, 30, 0))
        empty_book._add_order(Order(3, 102, Side.BUY, 999, 40, 0))

        orders = empty_book.get_orders_at_price(Side.BUY, 1000)
        assert len(orders) == 2
        assert orders[0].order_id == 1
        assert orders[1].order_id == 2

    def test_get_orders_at_price_empty(self, empty_book):
        """get_orders_at_price should return empty list for nonexistent price."""
        orders = empty_book.get_orders_at_price(Side.BUY, 1000)
        assert orders == []


# =============================================================================
# Reconstruct Function Tests
# =============================================================================


class TestReconstructAt:
    """Tests for the reconstruct_at function."""

    def test_reconstruct_at_beginning(self, sample_deltas_file):
        """Reconstructing at timestamp 0 should apply initial orders."""
        book = reconstruct_at(sample_deltas_file, 0)

        assert book.timestamp == 0
        assert book.get_order(1) is not None
        assert book.get_order(2) is not None

    def test_reconstruct_at_middle(self, sample_deltas_file):
        """Reconstructing at middle timestamp should apply deltas up to that point."""
        book = reconstruct_at(sample_deltas_file, 20)

        order1 = book.get_order(1)
        assert order1 is not None
        assert order1.quantity == 70

    def test_reconstruct_at_end(self, sample_deltas_file):
        """Reconstructing at final timestamp should apply all deltas."""
        book = reconstruct_at(sample_deltas_file, 50)

        assert book.get_order(1) is None
        assert book.get_order(2) is not None
        assert book.get_order(4) is not None

    def test_reconstruct_before_any_delta(self, sample_deltas_file):
        """Reconstructing before any delta should give empty book."""
        book = reconstruct_at(sample_deltas_file, -10)

        assert len(book.bids) == 0
        assert len(book.asks) == 0


# =============================================================================
# Edge Cases and Regression Tests
# =============================================================================


class TestEdgeCases:
    """Tests for edge cases and potential bugs."""

    def test_cancel_already_removed_order(self, empty_book):
        """Canceling non-existent order should not crash."""
        cancel_delta = make_delta(20, "CANCEL", 999, 100, "BUY", 1000, 50, 50)
        empty_book.apply_delta(cancel_delta)

    def test_fill_already_removed_order(self, empty_book):
        """Filling non-existent order should not crash."""
        fill_delta = make_delta(20, "FILL", 999, 100, "BUY", 1000, 50, 0)
        empty_book.apply_delta(fill_delta)

    def test_reverse_on_empty_book(self, empty_book):
        """Reversing delta on empty book should not crash."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        empty_book.apply_reverse_delta(add_delta, prev_timestamp=0)

    def test_multiple_orders_same_timestamp(self, empty_book):
        """Multiple orders added at same timestamp should all be present."""
        delta1 = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        delta2 = make_delta(10, "ADD", 2, 101, "BUY", 998, 30, 30)
        delta3 = make_delta(10, "ADD", 3, 102, "SELL", 1001, 40, 40)

        empty_book.apply_delta(delta1)
        empty_book.apply_delta(delta2)
        empty_book.apply_delta(delta3)

        assert empty_book.get_order(1) is not None
        assert empty_book.get_order(2) is not None
        assert empty_book.get_order(3) is not None

    def test_order_timestamp_preserved_on_reverse_fill(self, empty_book):
        """Order's original timestamp should be restored when reversing complete fill."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        fill_delta = make_delta(50, "FILL", 1, 100, "BUY", 999, 50, 0)

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(fill_delta)

        empty_book.apply_reverse_delta(fill_delta, prev_timestamp=10)

        order = empty_book.get_order(1)
        assert order is not None
        assert order.timestamp == 10

    def test_order_timestamp_preserved_on_reverse_cancel(self, empty_book):
        """Order's original timestamp should be restored when reversing cancel."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 50, 50)
        cancel_delta = make_delta(50, "CANCEL", 1, 100, "BUY", 999, 50, 50)

        empty_book.apply_delta(add_delta)
        empty_book.apply_delta(cancel_delta)

        empty_book.apply_reverse_delta(cancel_delta, prev_timestamp=10)

        order = empty_book.get_order(1)
        assert order is not None
        assert order.timestamp == 10

    def test_reverse_multiple_fills_same_order(self, empty_book):
        """Multiple partial fills should reverse correctly."""
        add_delta = make_delta(10, "ADD", 1, 100, "BUY", 999, 100, 100)
        fill1 = make_delta(20, "FILL", 1, 100, "BUY", 999, 30, 70)
        fill2 = make_delta(30, "FILL", 1, 100, "BUY", 999, 40, 30)
        fill3 = make_delta(40, "FILL", 1, 100, "BUY", 999, 30, 0)

        empty_book.apply_delta(add_delta)
        state_after_add = get_book_state(empty_book)

        empty_book.apply_delta(fill1)
        state_after_fill1 = get_book_state(empty_book)

        empty_book.apply_delta(fill2)
        state_after_fill2 = get_book_state(empty_book)

        empty_book.apply_delta(fill3)

        empty_book.apply_reverse_delta(fill3, prev_timestamp=30)
        assert states_equal(state_after_fill2, get_book_state(empty_book))

        empty_book.apply_reverse_delta(fill2, prev_timestamp=20)
        assert states_equal(state_after_fill1, get_book_state(empty_book))

        empty_book.apply_reverse_delta(fill1, prev_timestamp=10)
        assert states_equal(state_after_add, get_book_state(empty_book))


# =============================================================================
# Integration Tests with Real Data Format
# =============================================================================


class TestRealDataFormat:
    """Tests using the actual CSV format from the simulation."""

    def test_parse_real_csv_row(self, sample_deltas_file):
        """Verify that real CSV format is parsed correctly."""
        deltas = list(read_deltas(sample_deltas_file))

        first_delta = deltas[0]
        assert first_delta["timestamp"] == "0"
        assert first_delta["delta_type"] == "ADD"
        assert first_delta["order_id"] == "1"
        assert first_delta["side"] == "BUY"
        assert first_delta["price"] == "999"

    def test_full_replay_gives_consistent_results(self, sample_deltas_file):
        """Full replay should give same result as reconstruct_at."""
        final_timestamp = 50

        book1 = reconstruct_at(sample_deltas_file, final_timestamp)

        book2 = OrderBook()
        for delta in read_deltas(sample_deltas_file):
            if int(delta["timestamp"]) > final_timestamp:
                break
            book2.apply_delta(delta)

        state1 = get_book_state(book1)
        state2 = get_book_state(book2)

        assert states_equal(state1, state2)


# =============================================================================
# Integration Test with Actual Output File
# =============================================================================


class TestActualDeltasFile:
    """Integration tests using a copy of actual simulation output."""

    @pytest.fixture
    def actual_deltas_path(self):
        """Path to a copy of deltas.csv stored in tests/data/."""
        path = os.path.join(
            os.path.dirname(__file__), "..", "tests", "data", "deltas.csv"
        )
        if not os.path.exists(path):
            pytest.skip("tests/data/deltas.csv not found")
        return path

    def test_full_forward_backward_consistency(self, actual_deltas_path):
        """Full forward then backward scan should return to original state at each step."""
        index = DeltaIndex(actual_deltas_path)

        def snapshot(book):
            bids = {}
            for p in book.bids.keys():
                bids[p] = [(o.order_id, o.quantity) for o in book.bids[p]]
            asks = {}
            for p in book.asks.keys():
                asks[p] = [(o.order_id, o.quantity) for o in book.asks[p]]
            return (book.timestamp, dict(book.registry), bids, asks)

        book = OrderBook()
        states = [snapshot(book)]
        all_delta_groups = []

        # Build forward
        for i in range(len(index)):
            deltas = index.read_deltas_at_index(i)
            all_delta_groups.append(deltas)
            for delta in deltas:
                book.apply_delta(delta)
            states.append(snapshot(book))

        # Reverse back
        mismatches = []
        for i in range(len(index) - 1, -1, -1):
            prev_ts = index.timestamps[i - 1] if i > 0 else 0
            for delta in reversed(all_delta_groups[i]):
                book.apply_reverse_delta(delta, prev_timestamp=prev_ts)

            expected = states[i]
            actual = snapshot(book)

            if expected != actual:
                mismatches.append(i)

        assert (
            len(mismatches) == 0
        ), f"State mismatches at indices: {mismatches[:10]}..."


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
