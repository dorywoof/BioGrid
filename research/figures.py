import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from common import FIGURES, RESULTS

HIGHLIGHT = "#2a78d6"
HIGHLIGHT_DARK = "#174a8c"
CONTEXT = "#a3a29c"
CONTEXT_DARK = "#5f5e59"
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SOFT = "#52514e"
GRID = "#e6e5e0"
SENSES = ["humidity (VP5)", "cooling (VP3a)", "heating (VP2)"]


def style(ax, title):
    ax.set_facecolor(SURFACE)
    ax.set_title(title, loc="left", fontsize=11, color=INK, pad=10)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK_SOFT, labelsize=8.5)
    ax.set_axisbelow(True)


def steering_figure(knockouts):
    intact = knockouts[knockouts.kind == "intact"]
    fig, axes = plt.subplots(1, 3, figsize=(11, 3.4), sharey=True, facecolor=SURFACE)
    for ax, sense in zip(axes, SENSES):
        rows = intact[intact.sense == sense].set_index("stimulated")
        x = np.arange(2)
        left = [rows.loc["left", "dnb05_left_hz"], rows.loc["right", "dnb05_left_hz"]]
        right = [rows.loc["left", "dnb05_right_hz"], rows.loc["right", "dnb05_right_hz"]]
        ax.bar(x - 0.18, left, 0.34, color=HIGHLIGHT, label="DNb05 left")
        ax.bar(x + 0.18, right, 0.34, color=CONTEXT, label="DNb05 right")
        for xi, value in zip(list(x - 0.18) + list(x + 0.18), left + right):
            ax.text(xi, value + 2, f"{value:.0f}", ha="center", fontsize=8.5, color=INK)
        ax.set_xticks(x)
        ax.set_xticklabels(["left antenna", "right antenna"], fontsize=9, color=INK)
        ax.yaxis.grid(True, color=GRID, linewidth=0.8)
        style(ax, sense)
    axes[0].set_ylabel("firing rate (Hz)", color=INK_SOFT, fontsize=9)
    axes[0].legend(frameon=False, fontsize=8.5, loc="upper right")
    fig.tight_layout()
    fig.savefig(FIGURES / "fig1_dnb05_lateralization.png", dpi=160, facecolor=SURFACE)


def single_knockout_figure(knockouts, family):
    singles = knockouts[knockouts.kind == "single"].copy()
    base = knockouts[(knockouts.kind == "intact") & (knockouts.stimulated == "left")].set_index("sense").dnb05_left_hz
    singles["drop"] = 100 * (1 - singles.dnb05_left_hz / singles.sense.map(base))
    family_ids = set(family["index"].astype(str))
    fig, axes = plt.subplots(1, 2, figsize=(11, 3.8), facecolor=SURFACE)
    for ax, sense in zip(axes, SENSES[:2]):
        top = singles[singles.sense == sense].nlargest(10, "drop")
        labels = [str(c) for c in top.cell_type]
        colors = [HIGHLIGHT if m in family_ids else CONTEXT for m in top.muted]
        y = np.arange(len(top))[::-1]
        ax.barh(y, top["drop"], color=colors, height=0.6)
        for yi, value in zip(y, top["drop"]):
            ax.text(value + 0.8, yi, f"{value:.0f}%", va="center", fontsize=8.5, color=INK)
        ax.set_yticks(y)
        ax.set_yticklabels(labels, fontsize=8.5, color=INK)
        ax.xaxis.grid(True, color=GRID, linewidth=0.8)
        ax.set_xlim(0, max(10, top["drop"].max() * 1.2))
        ax.set_xlabel("drop in ipsilateral DNb05 response when silenced", color=INK_SOFT, fontsize=9)
        style(ax, f"Single-neuron knockouts, {sense}")
    fig.text(0.01, 0.01, "Blue: multiglomerular VP projection neurons (VP5+ / VP3+ / VP2+ l2PN).", fontsize=8, color=INK_SOFT)
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(FIGURES / "fig2_single_knockouts.png", dpi=160, facecolor=SURFACE)


def ipsilateral(frame):
    left = frame[frame.stimulated == "left"].dnb05_left_hz.to_numpy()
    right = frame[frame.stimulated == "right"].dnb05_right_hz.to_numpy()
    return (left + right) / 2


def group_knockout_figure(knockouts):
    fig, ax = plt.subplots(figsize=(9, 4.0), facecolor=SURFACE)
    x = np.arange(len(SENSES))
    for k, sense in enumerate(SENSES):
        rows = knockouts[knockouts.sense == sense]
        intact = ipsilateral(rows[rows.kind == "intact"])[0]
        family = ipsilateral(rows[rows.kind == "family"])[0]
        randoms = [ipsilateral(rows[rows.kind == kind])[0] for kind in sorted(rows.kind.unique()) if kind.startswith("random")]
        ax.bar(k - 0.27, intact, 0.25, color=CONTEXT_DARK, label="intact" if k == 0 else None)
        low, high = np.percentile(randoms, [5, 95])
        ax.bar(k, np.mean(randoms), 0.25, color=CONTEXT, label=f"random 16 silenced (n = {len(randoms)}, 5-95%)" if k == 0 else None)
        ax.errorbar(k, np.mean(randoms), yerr=[[np.mean(randoms) - low], [high - np.mean(randoms)]], color=CONTEXT_DARK,
                    capsize=3, linewidth=1)
        ax.bar(k + 0.27, family, 0.25, color=HIGHLIGHT, label="16 VP projection neurons silenced" if k == 0 else None)
        for xi, value in ((k - 0.27, intact), (k + 0.27, family)):
            ax.text(xi, value + 2, f"{value:.0f}", ha="center", fontsize=8.5, color=INK)
    ax.set_xticks(x)
    ax.set_xticklabels(SENSES, fontsize=9.5, color=INK)
    ax.set_ylabel("ipsilateral DNb05 (Hz)", color=INK_SOFT, fontsize=9)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8)
    ax.legend(frameon=False, fontsize=8.5, loc="upper center", bbox_to_anchor=(0.5, -0.12), ncol=3)
    style(ax, "Group knockout vs. size-matched random knockouts")
    fig.tight_layout()
    fig.savefig(FIGURES / "fig3_group_knockout.png", dpi=160, facecolor=SURFACE)


def main():
    FIGURES.mkdir(exist_ok=True)
    knockouts = pd.read_csv(RESULTS / "knockouts.csv", dtype={"muted": str, "cell_type": str}).fillna("")
    family = pd.read_csv(RESULTS / "vp_projection_family.csv")
    steering_figure(knockouts)
    single_knockout_figure(knockouts, family)
    group_knockout_figure(knockouts)
    print("saved figures to", FIGURES)


if __name__ == "__main__":
    main()
