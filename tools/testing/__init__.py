"""
Cross-validation testing infrastructure for market simulator.

This package provides tools for validating that the Python replay engine
produces exactly the same state as the C++ simulation engine.
"""

from .state_comparator import StateComparator, ComparisonResult
from .pnl_tracker import PnLTracker

__all__ = [
    "StateComparator",
    "ComparisonResult",
    "PnLTracker",
]
