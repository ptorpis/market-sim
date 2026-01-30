"""
P&L tracking for cross-validation testing.

Mirrors the C++ P&L tracking logic to validate that Python and C++
produce identical position and cash states.
"""

from dataclasses import dataclass, field
from typing import Optional
import csv
from pathlib import Path


@dataclass
class PnLState:
    """P&L state for a single participant."""

    long_position: int = 0
    short_position: int = 0
    cash: int = 0

    @property
    def net_position(self) -> int:
        """Net position (long - short)."""
        return self.long_position - self.short_position

    def to_dict(self) -> dict:
        """Convert to dictionary matching C++ JSON format."""
        return {
            "long_position": self.long_position,
            "short_position": self.short_position,
            "cash": self.cash,
        }


class PnLTracker:
    """
    Track P&L state to mirror C++ simulation.

    Processes trades from trades.csv and maintains position/cash state
    for all participants. The logic matches SimulationEngine::notify_trade().

    Usage:
        tracker = PnLTracker()
        tracker.on_trade(buyer_id=1, seller_id=2, price=1000, quantity=50)
        state = tracker.get_state()  # {1: {...}, 2: {...}}
    """

    def __init__(self):
        self.pnl: dict[int, PnLState] = {}

    def _ensure_client(self, client_id: int) -> PnLState:
        """Ensure client exists in tracker and return their PnL state."""
        if client_id not in self.pnl:
            self.pnl[client_id] = PnLState()
        return self.pnl[client_id]

    def on_trade(
        self, buyer_id: int, seller_id: int, price: int, quantity: int
    ) -> None:
        """
        Update P&L for a trade.

        Matches the C++ logic in SimulationEngine::notify_trade():
        - Buyer: long_position += quantity, cash -= (quantity * price)
        - Seller: short_position += quantity, cash += (quantity * price)

        Args:
            buyer_id: Client ID of the buyer
            seller_id: Client ID of the seller
            price: Trade price
            quantity: Trade quantity
        """
        trade_value = quantity * price

        # Update buyer
        buyer_pnl = self._ensure_client(buyer_id)
        buyer_pnl.long_position += quantity
        buyer_pnl.cash -= trade_value

        # Update seller
        seller_pnl = self._ensure_client(seller_id)
        seller_pnl.short_position += quantity
        seller_pnl.cash += trade_value

    def get_state(self) -> dict[int, dict]:
        """
        Get current P&L state for all participants.

        Returns:
            Dict mapping client_id (int) -> PnL dict with fields:
            - long_position
            - short_position
            - cash
        """
        return {client_id: pnl.to_dict() for client_id, pnl in self.pnl.items()}

    def get_client_pnl(self, client_id: int) -> Optional[dict]:
        """Get P&L for a specific client, or None if not found."""
        if client_id in self.pnl:
            return self.pnl[client_id].to_dict()
        return None

    def total_cash(self) -> int:
        """Sum of all participants' cash (should be zero in closed system)."""
        return sum(pnl.cash for pnl in self.pnl.values())

    def total_net_position(self) -> int:
        """Sum of all participants' net positions (should be zero in closed system)."""
        return sum(pnl.net_position for pnl in self.pnl.values())

    def reset(self) -> None:
        """Clear all P&L state."""
        self.pnl.clear()

    @classmethod
    def from_trades_csv(cls, trades_file: Path) -> "PnLTracker":
        """
        Create tracker by processing all trades from a CSV file.

        Args:
            trades_file: Path to trades.csv from C++ simulation

        Returns:
            PnLTracker with all trades applied
        """
        tracker = cls()

        with open(trades_file, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                tracker.on_trade(
                    buyer_id=int(row["buyer_id"]),
                    seller_id=int(row["seller_id"]),
                    price=int(row["price"]),
                    quantity=int(row["quantity"]),
                )

        return tracker

    def load_trades_up_to(
        self, trades_file: Path, max_timestamp: int
    ) -> "PnLTracker":
        """
        Load trades up to and including a specific timestamp.

        Args:
            trades_file: Path to trades.csv
            max_timestamp: Maximum timestamp to include

        Returns:
            Self (for chaining)
        """
        with open(trades_file, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                ts = int(row["timestamp"])
                if ts > max_timestamp:
                    break

                self.on_trade(
                    buyer_id=int(row["buyer_id"]),
                    seller_id=int(row["seller_id"]),
                    price=int(row["price"]),
                    quantity=int(row["quantity"]),
                )

        return self
