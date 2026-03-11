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

    _groups: dict[int, list] = {}
    for _rd in _df.itertuples(index=False):
        _ts = int(_rd.timestamp)
        if _ts not in _groups:
            _groups[_ts] = []
        _groups[_ts].append(_rd._asdict())

    _timestamps = sorted(_groups.keys())

    _book  = OrderBook()
    _frames: list[tuple[int, list, list]] = []
    _snaps: list[dict] = []

    for _ts in _timestamps:
        for _delta in _groups[_ts]:
            _book.apply_delta(_delta)

        _bid, _ask = _book.get_full_depth()
        _frames.append((_ts, list(_bid), list(_ask)))

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
# Navigation state
# ---------------------------------------------------------------------------

@app.cell
def _nav_state(mo):
    get_idx, set_idx = mo.state(0)
    return get_idx, set_idx


# ---------------------------------------------------------------------------
# Navigation + levels control bar
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


# ---------------------------------------------------------------------------
# Depth chart + L2 book table side by side
# ---------------------------------------------------------------------------

@app.cell
def _book_view(mo, plt, pd, frames, get_idx, BID_COLOR, ASK_COLOR):
    _ts, _bid_levels, _ask_levels = frames[get_idx()]

    # --- Cumulative depth data ---
    _bid_prices = [p for p, _ in reversed(_bid_levels)]
    _bid_cum, _total = [], 0
    for _, _q in reversed(_bid_levels):
        _total += _q
        _bid_cum.append(_total)
    _bid_cum = list(reversed(_bid_cum))

    _ask_prices = [p for p, _ in _ask_levels]
    _ask_cum, _total = [], 0
    for _, _q in _ask_levels:
        _total += _q
        _ask_cum.append(_total)

    _best_bid = _bid_levels[0][0] if _bid_levels else None
    _best_ask = _ask_levels[0][0] if _ask_levels else None
    _mid    = ((_best_bid + _best_ask) / 2) if (_best_bid and _best_ask) else None
    _spread = (_best_ask - _best_bid)       if (_best_bid and _best_ask) else "—"
    _mid_str = f"{_mid:.1f}" if _mid else "—"

    _fig, _ax = plt.subplots(figsize=(10, 6))
    _ax.set_title(f"t={_ts}   Spread: {_spread}   Mid: {_mid_str}")
    if _bid_prices:
        _ax.fill_between(
            _bid_prices, _bid_cum, step="post", alpha=0.35, color=BID_COLOR
        )
        _ax.step(
            _bid_prices, _bid_cum, where="post",
            color=BID_COLOR, label="Bids", linewidth=1.5,
        )
    if _ask_prices:
        _ax.fill_between(
            _ask_prices, _ask_cum, step="post", alpha=0.35, color=ASK_COLOR
        )
        _ax.step(
            _ask_prices, _ask_cum, where="post",
            color=ASK_COLOR, label="Asks", linewidth=1.5,
        )
    if _mid:
        _ax.axvline(
            _mid, color="white", linewidth=0.9,
            linestyle="--", label=f"Mid {_mid:.1f}",
        )
    _ax.set_xlabel("Price")
    _ax.set_ylabel("Cumulative Quantity")
    _ax.legend()
    plt.tight_layout()

    # --- L2 tables ---
    bid_table = mo.ui.table(
        pd.DataFrame(_bid_levels, columns=["Price", "Qty"]),
        selection="single",
        label=f"Bids  (best: {_best_bid})",
        show_column_summaries=False,
        page_size=15,
    )
    ask_table = mo.ui.table(
        pd.DataFrame(_ask_levels, columns=["Price", "Qty"]),
        selection="single",
        label=f"Asks  (best: {_best_ask})",
        show_column_summaries=False,
        page_size=15,
    )

    mo.hstack([
        mo.as_html(_fig),
        mo.vstack([
            mo.md(f"## Order Book"),
            mo.hstack([bid_table, ask_table]),
        ]),
    ])
    return bid_table, ask_table


# ---------------------------------------------------------------------------
# Orders at selected level (click a row in the L2 table above)
# ---------------------------------------------------------------------------

@app.cell
def _level_orders(mo, pd, snapshots, get_idx, bid_table, ask_table):
    _snap = snapshots[get_idx()]

    _side  = None
    _price = None

    _bid_sel = bid_table.value
    _ask_sel = ask_table.value

    if _bid_sel is not None and not _bid_sel.empty:
        _price = int(_bid_sel.iloc[0]["Price"])
        _side  = "BUY"
    elif _ask_sel is not None and not _ask_sel.empty:
        _price = int(_ask_sel.iloc[0]["Price"])
        _side  = "SELL"

    if _price is not None:
        _orders = _snap["bids" if _side == "BUY" else "asks"].get(_price, [])
        _df = pd.DataFrame(_orders, columns=["order_id", "client_id", "quantity"])
        level_table = mo.ui.table(_df, selection="single", show_column_summaries=False)
        _out = mo.vstack([mo.md(f"### {_side} @ {_price}"), level_table])
    else:
        level_table = mo.ui.table(
            pd.DataFrame(columns=["order_id", "client_id", "quantity"]),
            selection="single",
            show_column_summaries=False,
        )
        _out = mo.md("_Click a price level in the book above to inspect its orders._")
    _out
    return (level_table,)


# ---------------------------------------------------------------------------
# Order inspector by ID (o command)
# ---------------------------------------------------------------------------

@app.cell
def _order_inspector(mo):
    order_id_input = mo.ui.number(
        start=0, stop=10_000_000, step=1, value=0,
        label="Inspect order by ID (o)",
    )
    order_id_input
    return (order_id_input,)


@app.cell
def _order_display(mo, pd, snapshots, get_idx, order_id_input, level_table):
    _reg = snapshots[get_idx()]["registry"]

    _level_sel = level_table.value
    if _level_sel is not None and not _level_sel.empty:
        _oid = int(_level_sel.iloc[0]["order_id"])
    else:
        mo.stop(
            order_id_input.value is None,
            mo.md("_Enter an order ID to inspect._"),
        )
        _oid = int(order_id_input.value)

    mo.stop(
        _oid not in _reg,
        mo.md(f"_Order {_oid} not in book at this timestamp._"),
    )
    mo.ui.table(pd.DataFrame([_reg[_oid]]))


if __name__ == "__main__":
    app.run()
