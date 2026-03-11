"""
GM Replication Results Browser

Interactive dashboard for browsing sweep results produced by
tools/experiments/gm_replication_v2.py (or any compatible sweep).

Usage:
    marimo run tools/dashboards/gm_results.py
    marimo edit tools/dashboards/gm_results.py   # editable mode
"""

import marimo

__generated_with = "0.20.4"
app = marimo.App(width="full", app_title="GM Results Browser")


@app.cell
def _imports():
    import marimo as mo
    import pandas as pd
    import numpy as np
    import matplotlib.pyplot as plt
    import seaborn as sns
    from pathlib import Path

    sns.set_theme(style="whitegrid", palette="tab10", font_scale=1.1)
    return mo, pd, np, plt, sns, Path


# ---------------------------------------------------------------------------
# Directory picker + data loading
# ---------------------------------------------------------------------------

@app.cell
def _dir_input(mo):
    results_dir_input = mo.ui.text(
        value="/mnt/toshiba/market-sim/gm_replication_v2",
        label="Results directory",
        full_width=True,
    )
    results_dir_input
    return (results_dir_input,)


@app.cell
def _load(mo, pd, Path, results_dir_input):
    _dir = Path(results_dir_input.value)
    _results_csv = _dir / "results.csv"
    _breakeven_csv = _dir / "breakeven.csv"

    mo.stop(
        not _results_csv.exists(),
        mo.callout(mo.md(f"**results.csv not found** in `{_dir}`"), kind="danger"),
    )

    results = pd.read_csv(_results_csv)
    breakevens = pd.read_csv(_breakeven_csv) if _breakeven_csv.exists() else None

    n_total = len(results)
    n_failed = int(results["mm_avg_passive_edge"].isna().sum())
    sigma_vals = sorted(results["sigma"].unique())
    pi_vals = sorted(results["pi"].unique())
    spread_vals = sorted(results["half_spread"].unique())

    return results, breakevens, n_total, n_failed, sigma_vals, pi_vals, spread_vals


# ---------------------------------------------------------------------------
# Header + summary stats
# ---------------------------------------------------------------------------

@app.cell
def _summary(mo, n_total, n_failed, sigma_vals, pi_vals, spread_vals, breakevens):
    _cells_total = len(sigma_vals) * len(pi_vals)
    _cells_valid = (
        int(breakevens["breakeven_half_spread"].notna().sum())
        if breakevens is not None else "N/A"
    )
    mo.vstack([
        mo.md("# GM Replication — Results Browser"),
        mo.hstack([
            mo.stat(label="Total runs",       value=str(n_total)),
            mo.stat(label="Failed",           value=str(n_failed), bordered=True),
            mo.stat(label="σ values",         value=str(len(sigma_vals))),
            mo.stat(label="π values",         value=str(len(pi_vals))),
            mo.stat(label="Spread points",    value=str(len(spread_vals))),
            mo.stat(label="Valid breakevens", value=f"{_cells_valid}/{_cells_total}"),
        ], justify="start"),
    ])
    return ()


# ---------------------------------------------------------------------------
# Breakeven table
# ---------------------------------------------------------------------------

@app.cell
def _breakeven_table(mo, breakevens):
    mo.stop(breakevens is None, mo.callout(mo.md("No breakeven.csv found."), kind="warn"))

    _pivot = (
        breakevens
        .pivot(index="sigma", columns="pi", values="breakeven_half_spread")
        .round(1)
    )
    _pivot.index.name = "σ \\ π"
    _pivot.columns = [f"π={p:.2f}" for p in _pivot.columns]

    mo.vstack([
        mo.md("## Breakeven Half-Spread Table"),
        mo.ui.table(_pivot.reset_index()),
    ])
    return ()


# ---------------------------------------------------------------------------
# Breakeven plots
# ---------------------------------------------------------------------------

@app.cell
def _breakeven_plots(mo, plt, sns, breakevens):
    mo.stop(breakevens is None)

    _data = breakevens.dropna(subset=["breakeven_half_spread"]).copy()
    _data["σ"] = _data["sigma"].map(lambda s: f"σ={s}")
    _data["π"] = _data["pi"].map(lambda p: f"π={p:.2f}")

    _fig, _axes = plt.subplots(1, 2, figsize=(14, 5))

    sns.lineplot(
        data=_data, x="pi", y="breakeven_half_spread",
        hue="σ", marker="o", ax=_axes[0],
    )
    _axes[0].set_xlabel("π  (informed trader fraction)")
    _axes[0].set_ylabel("Equilibrium half-spread (price units)")
    _axes[0].set_title("Equilibrium Spread vs π")
    _axes[0].legend(title=None)

    sns.lineplot(
        data=_data, x="sigma", y="breakeven_half_spread",
        hue="π", marker="o", ax=_axes[1],
    )
    _axes[1].set_xlabel("σ  (GBM volatility)")
    _axes[1].set_ylabel("Equilibrium half-spread (price units)")
    _axes[1].set_title("Equilibrium Spread vs σ")
    _axes[1].legend(title=None)

    plt.tight_layout()
    mo.as_html(_fig)
    return ()


# ---------------------------------------------------------------------------
# MM passive edge curves
# — controls defined in their own cells so the plot cell can read .value
# ---------------------------------------------------------------------------

@app.cell
def _pnl_sigma_dd(mo, sigma_vals):
    pnl_sigma_dd = mo.ui.dropdown(
        options={f"σ={s}": s for s in sigma_vals},
        value=f"σ={sigma_vals[-1]}",
        label="Volatility (σ)",
    )
    return (pnl_sigma_dd,)


@app.cell
def _pnl_pi_ms(mo, pi_vals):
    pnl_pi_ms = mo.ui.multiselect(
        options={f"π={p:.2f}": p for p in pi_vals},
        value=[f"π={p:.2f}" for p in pi_vals],
        label="Informed fraction (π)",
    )
    return (pnl_pi_ms,)


@app.cell
def _pnl_header(mo, pnl_sigma_dd, pnl_pi_ms):
    mo.vstack([
        mo.md("## MM Passive Edge Curves"),
        mo.hstack([pnl_sigma_dd, pnl_pi_ms]),
    ])
    return ()


@app.cell
def _pnl_plot(mo, plt, sns, results, pnl_sigma_dd, pnl_pi_ms):
    mo.stop(
        not pnl_pi_ms.value,
        mo.callout(mo.md("Select at least one π value."), kind="warn"),
    )

    _selected_sigma = pnl_sigma_dd.value
    _selected_pis = pnl_pi_ms.value

    def _ci95(x):
        return 1.96 * x.std(ddof=1) / (len(x) ** 0.5) if len(x) > 1 else float("nan")

    _agg = (
        results
        .groupby(["sigma", "pi", "half_spread"])["mm_avg_passive_edge"]
        .agg(mean="mean", ci95=_ci95)
        .reset_index()
    )
    _data = _agg[
        (_agg["sigma"] == _selected_sigma) &
        (_agg["pi"].isin(_selected_pis))
    ]

    _palette = sns.color_palette("tab10", n_colors=len(_selected_pis))
    _fig, _ax = plt.subplots(figsize=(10, 5))

    for (_pi, _grp), _col in zip(_data.groupby("pi"), _palette):
        _grp = _grp.sort_values("half_spread")
        _ax.plot(_grp["half_spread"].values, _grp["mean"].values,
                 marker="o", color=_col, label=f"π={_pi:.2f}")
        _ax.fill_between(
            _grp["half_spread"].values,
            _grp["mean"].values - _grp["ci95"].values,
            _grp["mean"].values + _grp["ci95"].values,
            alpha=0.15, color=_col,
        )

    _ax.axhline(y=0, color="black", linestyle="--", linewidth=0.9)
    _ax.set_xlabel("Half-spread (price units)")
    _ax.set_ylabel("MM avg passive edge (price units)")
    _ax.set_title(f"MM Passive Edge vs Half-Spread  (σ={_selected_sigma})  — 95% CI")
    _ax.legend(title=None)
    plt.tight_layout()
    mo.as_html(_fig)
    return ()


# ---------------------------------------------------------------------------
# Run explorer — browse individual replicates
# — controls in separate cells to avoid name collisions with the PnL section
# ---------------------------------------------------------------------------

@app.cell
def _explorer_sigma_dd(mo, sigma_vals):
    explorer_sigma_dd = mo.ui.dropdown(
        options={f"σ={s}": s for s in sigma_vals},
        value=f"σ={sigma_vals[0]}",
        label="σ",
    )
    return (explorer_sigma_dd,)


@app.cell
def _explorer_pi_dd(mo, pi_vals):
    explorer_pi_dd = mo.ui.dropdown(
        options={f"π={p:.2f}": p for p in pi_vals},
        value=f"π={pi_vals[0]:.2f}",
        label="π",
    )
    return (explorer_pi_dd,)


@app.cell
def _explorer_spread_dd(mo, spread_vals):
    explorer_spread_dd = mo.ui.dropdown(
        options={str(s): s for s in spread_vals},
        value=str(spread_vals[len(spread_vals) // 2]),
        label="half_spread",
    )
    return (explorer_spread_dd,)


@app.cell
def _explorer_header(mo, explorer_sigma_dd, explorer_pi_dd, explorer_spread_dd):
    mo.vstack([
        mo.md("## Run Explorer"),
        mo.hstack([explorer_sigma_dd, explorer_pi_dd, explorer_spread_dd]),
    ])
    return ()


@app.cell
def _explorer_table(mo, results, explorer_sigma_dd, explorer_pi_dd, explorer_spread_dd):
    _mask = (
        (results["sigma"] == explorer_sigma_dd.value) &
        (results["pi"] == explorer_pi_dd.value) &
        (results["half_spread"] == explorer_spread_dd.value)
    )
    _cols = ["sigma", "pi", "half_spread", "mm_avg_passive_edge", "run_id", "output_dir"]
    _subset = results[_mask][_cols].copy()
    _subset["mm_avg_passive_edge"] = _subset["mm_avg_passive_edge"].round(3)
    mo.ui.table(_subset.reset_index(drop=True))
    return ()


if __name__ == "__main__":
    app.run()
