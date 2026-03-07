"""
Programmatic config dict construction for GM replication experiments.

Mirrors the JSON schema consumed by the C++ SimulationEngine config loader
(include/config/config_loader.hpp). All field names match those in
config_template.json exactly.
"""

from __future__ import annotations

# Fixed client IDs used across all experiment configs. The noise trader group
# occupies IDs [NOISE_TRADER_START_ID, NOISE_TRADER_START_ID + n_noise - 1].
# Informed traders start at INFORMED_TRADER_START_ID, well above the noise group.
MM_CLIENT_ID = 999
NOISE_TRADER_START_ID = 1
INFORMED_TRADER_START_ID = 500


def build_gm_config(
    sigma: float,
    pi: float,
    half_spread: int,
    n_total_traders: int = 20,
    *,
    duration: int = 500_000,
    pnl_snapshot_interval: int = 1_000,
    seed_base: int = 42,
    observation_noise: float = 0.0,
    inventory_skew_factor: float = 0.0,
    initial_price: int = 1_000_000,
    tick_size: int = 1_000,
    drift: float = 0.0,
    noise_trader_spread: int | None = None,
) -> dict:
    """
    Build a GM-experiment config dict for the C++ simulator.

    The market microstructure setup mirrors Glosten-Milgrom (1985):
      - n_noise noise traders (uninformed order flow)
      - n_informed informed traders (informed order flow, pi fraction of total)
      - 1 market maker with fixed half_spread (the subject of the experiment)
      - GBM fair price process (pure GM environment, no jumps)

    GM mapping:
      - pi = n_informed / n_total_traders
      - sigma = GBM volatility
      - equilibrium_spread = f(pi, sigma) — what we are measuring

    All IDs are deterministic: noise traders get IDs 1..n_noise, informed traders
    start at INFORMED_TRADER_START_ID, MM is always MM_CLIENT_ID (999).

    Args:
        sigma: GBM volatility (e.g. 0.005 means 0.5% per price tick).
        pi: Informed fraction of the total trader population (0 < pi < 1).
        half_spread: MM half-spread in price units (same units as initial_price).
        n_total_traders: Total noise + informed trader count (MM excluded).
        duration: Simulation duration in ticks.
        pnl_snapshot_interval: Ticks between PnL snapshots in pnl.csv.
        seed_base: Base RNG seed. The noise group gets seed_base, MM gets
                   seed_base+1, fair price gets seed_base+2, informed trader i
                   gets seed_base+100+i. Using the same seed_base for the same
                   (sigma, pi, replicate) ensures identical fair price paths
                   across the half_spread sweep, giving a controlled experiment.
        observation_noise: Fair price observation noise on InformedTrader (float).
                           0.0 = perfect information, the cleanest GM analog.
        inventory_skew_factor: MM inventory skew coefficient.
                               0.0 = pure GM (no inventory adjustment);
                               non-zero for inventory extension experiments.
        initial_price: Starting asset price in price units.
        tick_size: Time units per GBM step (GBM dt = simulation_ticks / tick_size).
        drift: GBM drift parameter (use 0.0 for symmetric GM experiments).
        noise_trader_spread: Price range for noise trader random order placement.
                             Defaults to max(half_spread * 10, 500) to ensure
                             consistent crossing probability across the spread sweep.
    """
    n_informed = max(1, round(pi * n_total_traders))
    n_noise = max(1, n_total_traders - n_informed)

    # NT spread wide enough that NTs frequently cross the MM's quotes, mimicking
    # the GM assumption that noise traders always trade at quoted prices.
    nt_spread = noise_trader_spread if noise_trader_spread is not None else max(half_spread * 10, 500)

    noise_group = {
        "count": n_noise,
        "start_client_id": NOISE_TRADER_START_ID,
        "base_seed": seed_base,
        "initial_wakeup_start": 10,
        "initial_wakeup_step": 10,
        "config": {
            "instrument": 1,
            "observation_noise": 0.0,
            "spread": nt_spread,
            "min_quantity": 10,
            "max_quantity": 50,
            "min_interval": 50,
            "max_interval": 200,
            "adverse_fill_threshold": nt_spread // 2,
            "stale_order_threshold": nt_spread,
            "latency_jitter": 0.0,
        },
    }

    informed_agents = [
        {
            "client_id": INFORMED_TRADER_START_ID + i,
            "type": "InformedTrader",
            "initial_wakeup": 100 + i * 50,
            "latency": 5,
            "seed": seed_base + 100 + i,
            "config": {
                "instrument": 1,
                "min_quantity": 10,
                "max_quantity": 50,
                "min_interval": 100,
                "max_interval": 500,
                "min_edge": 1,
                "observation_noise": observation_noise,
                "adverse_fill_threshold": nt_spread // 2,
                "stale_order_threshold": nt_spread,
                "latency_jitter": 0.0,
            },
        }
        for i in range(n_informed)
    ]

    mm_agent = {
        "client_id": MM_CLIENT_ID,
        "type": "MarketMaker",
        "initial_wakeup": 5,
        "latency": 5,
        "seed": seed_base + 1,
        "config": {
            "instrument": 1,
            "observation_noise": 0.0,
            "half_spread": half_spread,
            "quote_size": 50,
            "update_interval": 20,
            "inventory_skew_factor": inventory_skew_factor,
            "max_position": 1000,
            "latency_jitter": 0.0,
        },
    }

    return {
        "simulation": {
            "latency": 5,
            "duration": duration,
            "output_dir": "./output",  # overridden by --output flag in runner.py
            "pnl_snapshot_interval": pnl_snapshot_interval,
        },
        "instruments": [1],
        "fair_price": {
            "model": "gbm",
            "initial_price": initial_price,
            "drift": drift,
            "volatility": sigma,
            "tick_size": tick_size,
            "seed": seed_base + 2,
        },
        "noise_traders": noise_group,
        "agents": informed_agents + [mm_agent],
        # Seed the book with a resting bid and ask before agents wake up,
        # preventing empty-book issues in the first few ticks.
        "initial_orders": [
            {
                "instrument": 1,
                "side": "BUY",
                "price": initial_price - half_spread * 2,
                "quantity": 100,
            },
            {
                "instrument": 1,
                "side": "SELL",
                "price": initial_price + half_spread * 2,
                "quantity": 100,
            },
        ],
    }
