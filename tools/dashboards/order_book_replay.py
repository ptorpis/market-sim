"""
Order Book Replay Dashboard

Browse GM replication sweep results and replay the order book for any run.
Loads order_deltas.parquet from the archive directory.

Usage:
    marimo run tools/dashboards/order_book_replay.py
    marimo edit tools/dashboards/order_book_replay.py   # editable mode
"""

import marimo

__generated_with = "0.20.4"
app = marimo.App(width="full", app_title="Order Book Replay")


@app.cell
def _imports():
    import marimo as mo
    import pandas as pd
    import matplotlib.pyplot as plt
    import matplotlib.ticker as mticker
    import seaborn as sns
    from pathlib import Path

    from tools.visualizer.order_book import OrderBook

    BID_COLOR = sns.color_palette("deep")[2]
    ASK_COLOR = sns.color_palette("deep")[3]

    return mo, pd, plt, mticker, sns, Path, OrderBook, BID_COLOR, ASK_COLOR


# ---------------------------------------------------------------------------
# Directory inputs
# ---------------------------------------------------------------------------

@app.cell
def _dir_inputs(mo):
    sweep_dir_input = mo.ui.text(
        value="/mnt/toshiba/market-sim/gm_replication_v2",
        label="Sweep results directory (contains results.csv)",
        full_width=True,
    )
    archive_dir_input = mo.ui.text(
        value="/mnt/toshiba/market-sim/archive",
        label="Archive directory (contains <run_id>/order_deltas.parquet)",
        full_width=True,
    )
    mo.vstack([sweep_dir_input, archive_dir_input])
    return sweep_dir_input, archive_dir_input


# ---------------------------------------------------------------------------
# Load results.csv
# ---------------------------------------------------------------------------

@app.cell
def _load_results(mo, pd, Path, sweep_dir_input):
    _csv = Path(sweep_dir_input.value) / "results.csv"
    mo.stop(
        not _csv.exists(),
        mo.callout(
            mo.md(f"**results.csv not found** in `{sweep_dir_input.value}`"),
            kind="danger",
        ),
    )
    results = pd.read_csv(_csv)
    sigma_vals  = sorted(results["sigma"].unique())
    pi_vals     = sorted(results["pi"].unique())
    spread_vals = sorted(results["half_spread"].unique())
    return results, sigma_vals, pi_vals, spread_vals


# ---------------------------------------------------------------------------
# Run filter controls
# ---------------------------------------------------------------------------

@app.cell
def _filter_sigma(mo, sigma_vals):
    sigma_dd = mo.ui.dropdown(
        options={f"σ={s}": s for s in sigma_vals},
        value=f"σ={sigma_vals[0]}",
        label="Volatility (σ)",
    )
    return (sigma_dd,)


@app.cell
def _filter_pi(mo, pi_vals):
    pi_dd = mo.ui.dropdown(
        options={f"π={p:.2f}": p for p in pi_vals},
        value=f"π={pi_vals[0]:.2f}",
        label="Informed fraction (π)",
    )
    return (pi_dd,)


@app.cell
def _filter_spread(mo, spread_vals):
    spread_dd = mo.ui.dropdown(
        options={str(s): s for s in spread_vals},
        value=str(spread_vals[len(spread_vals) // 2]),
        label="Half-spread",
    )
    return (spread_dd,)


@app.cell
def _filter_header(mo, sigma_dd, pi_dd, spread_dd):
    mo.vstack([
        mo.md("## Select a Run"),
        mo.hstack([sigma_dd, pi_dd, spread_dd]),
    ])
    return ()


# ---------------------------------------------------------------------------
# Run picker table
# ---------------------------------------------------------------------------

@app.cell
def _run_table(mo, results, sigma_dd, pi_dd, spread_dd):
    _mask = (
        (results["sigma"]       == sigma_dd.value) &
        (results["pi"]          == pi_dd.value) &
        (results["half_spread"] == spread_dd.value)
    )
    _cols = ["run_id", "mm_avg_passive_edge", "sigma", "pi", "half_spread"]
    _subset = results[_mask][_cols].copy()
    _subset["mm_avg_passive_edge"] = _subset["mm_avg_passive_edge"].round(3)
    _subset = _subset.reset_index(drop=True)

    mo.stop(
        _subset.empty,
        mo.callout(mo.md("No runs match the selected filters."), kind="warn"),
    )

    run_table = mo.ui.table(_subset, selection="single", label="Pick a run")
    run_table
    return run_table, _subset


# ---------------------------------------------------------------------------
# Load + pre-render selected run
# Builds two parallel lists:
#   frames    — (timestamp, bid_levels, ask_levels) for plotting
#   snapshots — per-timestamp order registry for inspection
# ---------------------------------------------------------------------------

@app.cell
def _load_run(mo, pd, Path, OrderBook, archive_dir_input, run_table, _subset):
    mo.stop(
        run_table.value is None or run_table.value.empty,
        mo.callout(
            mo.md("Select a row from the table above to load that run."),
            kind="warn",
        ),
    )

    _row    = run_table.value.iloc[0]
    _run_id = str(_row["run_id"])
    _path   = Path(archive_dir_input.value) / _run_id / "order_deltas.parquet"

    mo.stop(
        not _path.exists(),
        mo.callout(mo.md(f"**Parquet not found:** `{_path}`"), kind="danger"),
    )

    _df = pd.read_parquet(_path)

    # Group rows by timestamp (parquet is already sorted)
    _groups: dict[int, list] = {}
    for _rd in _df.itertuples(index=False):
        _ts = int(_rd.timestamp)
        if _ts not in _groups:
            _groups[_ts] = []
        _groups[_ts].append(_rd._asdict())

    _timestamps = sorted(_groups.keys())

    # Single O(N) forward pass
    _book  = OrderBook()
    _frames: list[tuple[int, list, list]] = []
    _snaps: list[dict] = []

    for _ts in _timestamps:
        for _delta in _groups[_ts]:
            _book.apply_delta(_delta)

        _bid, _ask = _book.get_full_depth()
        _frames.append((_ts, list(_bid), list(_ask)))

        # Registry snapshot: order_id → {side, price, client_id, quantity, added}
        _reg: dict[int, dict] = {}
        for _price, _queue in _book.bids.items():
            for _o in _queue:
                _reg[_o.order_id] = {
                    "side": "BUY", "price": _price,
                    "client_id": _o.client_id, "quantity": _o.quantity,
                    "added_at": _o.timestamp,
                }
        for _price, _queue in _book.asks.items():
            for _o in _queue:
                _reg[_o.order_id] = {
                    "side": "SELL", "price": _price,
                    "client_id": _o.client_id, "quantity": _o.quantity,
                    "added_at": _o.timestamp,
                }

        _snaps.append({
            "bids": {
                p: [(o.order_id, o.client_id, o.quantity) for o in q]
                for p, q in _book.bids.items()
            },
            "asks": {
                p: [(o.order_id, o.client_id, o.quantity) for o in q]
                for p, q in _book.asks.items()
            },
            "registry": _reg,
        })

    del _book

    frames     = _frames
    snapshots  = _snaps
    n_ts       = len(_timestamps)
    t_start    = _timestamps[0]
    t_end      = _timestamps[-1]
    loaded_run = _run_id

    return frames, snapshots, n_ts, t_start, t_end, loaded_run


# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

@app.cell
def _summary(mo, loaded_run, n_ts, t_start, t_end):
    mo.vstack([
        mo.md(f"## Replay — `{loaded_run}`"),
        mo.hstack([
            mo.stat(label="Timestamps", value=str(n_ts)),
            mo.stat(label="Start",      value=str(t_start)),
            mo.stat(label="End",        value=str(t_end)),
            mo.stat(label="Duration",   value=str(t_end - t_start)),
        ], justify="start"),
    ])
    return ()


# ---------------------------------------------------------------------------
# Navigation state — source of truth for the current timestamp index
# ---------------------------------------------------------------------------

@app.cell
def _nav_state(mo):
    get_idx, set_idx = mo.state(0)
    return get_idx, set_idx


# ---------------------------------------------------------------------------
# Navigation controls: prev / next / jump
# ---------------------------------------------------------------------------

@app.cell
def _nav_controls(mo, n_ts, frames, get_idx, set_idx):
    _prev_btn = mo.ui.button(
        label="◀ Prev",
        on_click=lambda _: set_idx(max(0, get_idx() - 1)),
    )
    _next_btn = mo.ui.button(
        label="Next ▶",
        on_click=lambda _: set_idx(min(n_ts - 1, get_idx() + 1)),
    )
    _jump = mo.ui.number(
        start=0, stop=n_ts - 1, step=1, value=get_idx(),
        label="Jump to index",
        on_change=lambda v: set_idx(int(v)),
    )
    _cur_ts, _, _ = frames[get_idx()]
    mo.hstack(
        [_prev_btn, _next_btn, _jump,
         mo.md(f"**{get_idx()} / {n_ts - 1}  —  t = {_cur_ts}**")],
        justify="start",
    )
    return ()


# ---------------------------------------------------------------------------
# Ladder levels control (d command)
# ---------------------------------------------------------------------------

@app.cell
def _levels_ctrl(mo):
    levels_input = mo.ui.number(
        start=1, stop=50, step=1, value=12, label="Ladder levels (d)"
    )
    levels_input
    return (levels_input,)


# ---------------------------------------------------------------------------
# Inspection controls: o / l / t
# ---------------------------------------------------------------------------

@app.cell
def _inspect_controls(mo):
    order_id_input = mo.ui.number(
        start=0, stop=10_000_000, step=1, value=0, label="Inspect order (o)"
    )
    level_side = mo.ui.radio(
        options=["BUY", "SELL"], value="BUY", label="Level side (l)"
    )
    level_price = mo.ui.number(
        start=0, stop=1_000_000, step=1, value=0, label="Level price (l)"
    )
    mo.vstack([
        mo.md("## Inspect"),
        mo.hstack([order_id_input, level_side, level_price], justify="start"),
    ])
    return order_id_input, level_side, level_price


# ---------------------------------------------------------------------------
# Inspection display
# ---------------------------------------------------------------------------

@app.cell
def _inspect_display(
    mo, pd, snapshots, get_idx, order_id_input, level_side, level_price
):
    _snap = snapshots[get_idx()]
    _reg  = _snap["registry"]

    def _level_df(orders):
        return pd.DataFrame(orders, columns=["order_id", "client_id", "quantity"])

    # --- Top of book (t) ---
    _bb = max(_snap["bids"].keys()) if _snap["bids"] else None
    _ba = min(_snap["asks"].keys()) if _snap["asks"] else None
    _top_bid_df = _level_df(_snap["bids"].get(_bb, []))
    _top_ask_df = _level_df(_snap["asks"].get(_ba, []))

    _top_section = mo.vstack([
        mo.md(f"**Top of Book (t)**"),
        mo.md(f"Best Bid: `{_bb}`"),
        mo.ui.table(_top_bid_df) if not _top_bid_df.empty else mo.md("_(empty)_"),
        mo.md(f"Best Ask: `{_ba}`"),
        mo.ui.table(_top_ask_df) if not _top_ask_df.empty else mo.md("_(empty)_"),
    ])

    # --- Order inspector (o) ---
    _oid = int(order_id_input.value)
    if _oid in _reg:
        _o = _reg[_oid]
        _order_section = mo.vstack([
            mo.md(f"**Order {_oid}**"),
            mo.ui.table(pd.DataFrame([_o])),
        ])
    else:
        _order_section = mo.vstack([
            mo.md(f"**Order {_oid}**"),
            mo.md("_(not in book at this timestamp)_"),
        ])

    # --- Level inspector (l) ---
    _side  = level_side.value
    _price = int(level_price.value)
    _lvl_orders = _snap["bids" if _side == "BUY" else "asks"].get(_price, [])
    _lvl_df = _level_df(_lvl_orders)
    _level_section = mo.vstack([
        mo.md(f"**{_side} @ {_price} (l)**"),
        mo.ui.table(_lvl_df) if not _lvl_df.empty else mo.md("_(no orders)_"),
    ])

    mo.hstack([_top_section, _order_section, _level_section], justify="start")
    return ()


# ---------------------------------------------------------------------------
# Book plot: cumulative depth + ladder
# ---------------------------------------------------------------------------

@app.cell
def _book_plot(
    mo, plt, mticker, frames, get_idx, levels_input, BID_COLOR, ASK_COLOR
):
    _ts, _bid_levels, _ask_levels = frames[get_idx()]
    _n_levels = int(levels_input.value)

    # Cumulative depth
    _bid_prices_d = [p for p, _ in reversed(_bid_levels)]
    _bid_cum, _total = [], 0
    for _, _q in reversed(_bid_levels):
        _total += _q
        _bid_cum.append(_total)
    _bid_cum = list(reversed(_bid_cum))

    _ask_prices_d = [p for p, _ in _ask_levels]
    _ask_cum, _total = [], 0
    for _, _q in _ask_levels:
        _total += _q
        _ask_cum.append(_total)

    # Ladder
    _top_bids = dict(_bid_levels[:_n_levels])
    _top_asks = dict(_ask_levels[:_n_levels])
    _tower_prices   = sorted(_top_bids.keys() | _top_asks.keys())
    _tower_bid_qtys = [_top_bids.get(p, 0) for p in _tower_prices]
    _tower_ask_qtys = [_top_asks.get(p, 0) for p in _tower_prices]

    _best_bid = _bid_levels[0][0] if _bid_levels else None
    _best_ask = _ask_levels[0][0] if _ask_levels else None
    _mid    = ((_best_bid + _best_ask) / 2) if (_best_bid and _best_ask) else None
    _spread = (_best_ask - _best_bid)       if (_best_bid and _best_ask) else None

    _fig, (_ax_d, _ax_l) = plt.subplots(1, 2, figsize=(18, 7))
    _title = f"t={_ts}"
    if _mid:
        _title += (
            f"   Bid {_best_bid} / Ask {_best_ask}"
            f"   Spread {_spread}   Mid {_mid:.1f}"
        )
    _fig.suptitle(_title, fontsize=12)

    if _bid_prices_d:
        _ax_d.fill_between(
            _bid_prices_d, _bid_cum, step="post", alpha=0.35, color=BID_COLOR
        )
        _ax_d.step(
            _bid_prices_d, _bid_cum, where="post",
            color=BID_COLOR, label="Bids", linewidth=1.5,
        )
    if _ask_prices_d:
        _ax_d.fill_between(
            _ask_prices_d, _ask_cum, step="post", alpha=0.35, color=ASK_COLOR
        )
        _ax_d.step(
            _ask_prices_d, _ask_cum, where="post",
            color=ASK_COLOR, label="Asks", linewidth=1.5,
        )
    if _mid:
        _ax_d.axvline(
            _mid, color="white", linewidth=0.9,
            linestyle="--", label=f"Mid {_mid:.1f}",
        )
    _ax_d.set_xlabel("Price")
    _ax_d.set_ylabel("Cumulative Quantity")
    _ax_d.set_title("Cumulative Depth")
    _ax_d.legend()

    if _tower_prices:
        _y = list(range(len(_tower_prices)))
        _ax_l.barh(
            _y, [-q for q in _tower_bid_qtys],
            height=0.6, color=BID_COLOR, alpha=0.7, label="Bids",
        )
        _ax_l.barh(
            _y, _tower_ask_qtys,
            height=0.6, color=ASK_COLOR, alpha=0.7, label="Asks",
        )
        _ax_l.set_yticks(_y)
        _ax_l.set_yticklabels(_tower_prices, fontsize=9)
        _ax_l.axvline(0, color="white", linewidth=0.8)
        _x_max = max(
            max(_tower_bid_qtys, default=0),
            max(_tower_ask_qtys, default=0),
            1,
        )
        _ax_l.set_xlim(-_x_max * 1.15, _x_max * 1.15)
        _ax_l.xaxis.set_major_formatter(
            mticker.FuncFormatter(lambda x, _: str(int(abs(x))))
        )
        _ax_l.legend()
    _ax_l.set_xlabel("Quantity")
    _ax_l.set_ylabel("Price")
    _ax_l.set_title(f"Order Ladder  (top {_n_levels} levels/side)")

    plt.tight_layout(rect=(0, 0, 1, 0.95))
    mo.as_html(_fig)
    return ()


# ---------------------------------------------------------------------------
# Timeline: best bid / ask over all timestamps, with current position marker
# ---------------------------------------------------------------------------

@app.cell
def _timeline(mo, plt, frames, get_idx, BID_COLOR, ASK_COLOR):
    _all_ts  = [f[0] for f in frames]
    _all_bid = [f[1][0][0] if f[1] else None for f in frames]
    _all_ask = [f[2][0][0] if f[2] else None for f in frames]
    _cur_ts  = _all_ts[get_idx()]

    _fig2, _ax = plt.subplots(figsize=(18, 3))
    _ax.plot(_all_ts, _all_bid, color=BID_COLOR, linewidth=0.8, label="Best bid")
    _ax.plot(_all_ts, _all_ask, color=ASK_COLOR, linewidth=0.8, label="Best ask")
    _ax.axvline(
        _cur_ts, color="white", linewidth=1.2,
        linestyle="--", label=f"t={_cur_ts}",
    )
    _ax.set_xlabel("Timestamp")
    _ax.set_ylabel("Price")
    _ax.set_title("Best Bid / Ask Over Time")
    _ax.legend(fontsize=8)
    plt.tight_layout()
    mo.as_html(_fig2)
    return ()


if __name__ == "__main__":
    app.run()
