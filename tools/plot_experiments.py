import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy.stats import mannwhitneyu

ORDER = [
    "intact",
    "VP relay silenced",
    "Ir68a mutant (VP5 silenced)",
    "VP3a silenced",
    "DNb05 silenced",
    "anosmic",
]
LABELS = {
    "intact": "Intact whole brain",
    "VP relay silenced": "16 VP relay neurons silenced",
    "Ir68a mutant (VP5 silenced)": "Ir68a mutant  (VP5 humidity off)",
    "VP3a silenced": "VP3a cooling neurons off",
    "DNb05 silenced": "DNb05 silenced",
    "anosmic": "No sensory input",
}
HIGHLIGHT = "#2a78d6"
HIGHLIGHT_DOT = "#174a8c"
RELAY = "#d98a1c"
RELAY_DOT = "#8a5207"
CONTEXT = "#a3a29c"
CONTEXT_DOT = "#5f5e59"
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SOFT = "#52514e"
GRID = "#e6e5e0"


def tones(condition):
    if condition == "intact":
        return HIGHLIGHT, HIGHLIGHT_DOT, 0.9
    if condition == "VP relay silenced":
        return RELAY, RELAY_DOT, 0.85
    return CONTEXT, CONTEXT_DOT, 0.55


def panel(ax, data, column, title, unit):
    ys = np.arange(len(ORDER))[::-1]
    rng = np.random.default_rng(3)
    for y, condition in zip(ys, ORDER):
        values = data.loc[data.condition == condition, column].to_numpy()
        bar, dot, alpha = tones(condition)
        mean = values.mean()
        ax.barh(y, mean, height=0.5, color=bar, alpha=alpha, zorder=2)
        ax.scatter(values, y + rng.uniform(-0.17, 0.17, len(values)), s=11, color=dot, edgecolor=SURFACE,
                   linewidth=0.5, zorder=3)
        ax.text(mean + 0.6, y + 0.3, f"{mean:.1f}{unit}", va="center", ha="left", fontsize=9, color=INK)
    ax.set_yticks(ys)
    ax.set_yticklabels([LABELS[c] for c in ORDER], fontsize=9, color=INK)
    ax.set_title(title, loc="left", fontsize=11, color=INK, pad=10)
    ax.xaxis.grid(True, color=GRID, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(axis="x", colors=INK_SOFT, labelsize=8)
    ax.tick_params(axis="y", length=0)
    ax.set_xlim(left=0)


def statistics(data):
    rows = []
    base = data[data.condition == "intact"]
    for condition in ORDER[1:]:
        other = data[data.condition == condition]
        rows.append({
            "condition": condition,
            "water": other.drinks.mean(),
            "water_change": other.drinks.mean() / base.drinks.mean() - 1,
            "water_p": mannwhitneyu(base.drinks, other.drinks, alternative="two-sided").pvalue,
            "shade_s": other.shade_seconds.mean(),
            "shade_change": other.shade_seconds.mean() / base.shade_seconds.mean() - 1,
            "shade_p": mannwhitneyu(base.shade_seconds, other.shade_seconds, alternative="two-sided").pvalue,
            "deaths": other.deaths.mean(),
        })
    return pd.DataFrame(rows)


def main():
    parser = argparse.ArgumentParser(description="Plot BioGrid whole-brain lesion experiments.")
    parser.add_argument("csv", type=Path)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    data = pd.read_csv(args.csv)
    out = args.out or args.csv.with_name("experiments.png")
    seconds = int(data.seconds.iloc[0])
    seeds = data.groupby("condition").seed.nunique().min()

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2), sharey=True, facecolor=SURFACE)
    for ax in axes:
        ax.set_facecolor(SURFACE)
    panel(axes[0], data, "drinks", f"Water droplets found per {seconds} s flight", "")
    panel(axes[1], data, "shade_seconds", f"Seconds spent in cool shade per {seconds} s flight", " s")
    fig.text(0.01, 0.01, f"Bars: mean. Dots: individual flights ({seeds} seeds per condition).", fontsize=8, color=INK_SOFT)
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(out, dpi=160, facecolor=SURFACE)

    base = data[data.condition == "intact"]
    print(f"intact: water {base.drinks.mean():.1f}, shade {base.shade_seconds.mean():.1f} s, deaths {base.deaths.mean():.2f}")
    print(statistics(data).to_string(index=False, float_format=lambda v: f"{v:.3g}"))
    print(f"saved {out}")


if __name__ == "__main__":
    main()
