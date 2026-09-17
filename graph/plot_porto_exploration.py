#!/usr/bin/env python3
"""
Plot Porto exploration results from Benchmark::runPortoExplorationBenchmark().

Expected input:
    ../benchmark/porto_exploration_vertices.csv
    ../benchmark/porto_exploration_summary.csv

Expected vertices CSV columns:
    algorithm,vertex_id,x,y,dist,reached,processed,on_path,path_order

The path_order column is optional, but strongly recommended. Without it, the
script can still draw path vertices, but it cannot draw the final path line
in the correct order.

Output:
    ../benchmark/plots/porto_exploration_side_by_side.png
    ../benchmark/plots/porto_exploration_dijkstra.png
    ../benchmark/plots/porto_exploration_astar.png
    ../benchmark/plots/porto_exploration_summary.png

Usage:
    python3 plot_porto_exploration.py
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

import numpy as np
import pandas as pd
import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt


SCRIPT_DIR = Path(__file__).resolve().parent

# Same convention as the benchmark plotting script.
BENCHMARK_DIR = Path("../benchmark")
PLOTS_DIR = BENCHMARK_DIR / "plots"

VERTICES_FILE = BENCHMARK_DIR / "porto_exploration_vertices.csv"
SUMMARY_FILE = BENCHMARK_DIR / "porto_exploration_summary.csv"


def setup_style() -> None:
    plt.rcParams.update({
        "figure.facecolor": "white",
        "axes.facecolor": "white",
        "savefig.facecolor": "white",
        "axes.edgecolor": "#333333",
        "axes.grid": False,
        "font.size": 11,
        "axes.titlesize": 15,
        "axes.labelsize": 12,
        "legend.fontsize": 10,
        "figure.autolayout": False,
    })


def ensure_dirs() -> None:
    PLOTS_DIR.mkdir(parents=True, exist_ok=True)


def read_vertices() -> Optional[pd.DataFrame]:
    if not VERTICES_FILE.exists():
        print(f"[skip] Missing {VERTICES_FILE}")
        return None

    df = pd.read_csv(VERTICES_FILE)

    required = {
        "algorithm",
        "vertex_id",
        "x",
        "y",
        "dist",
        "reached",
        "processed",
        "on_path",
    }

    missing = required - set(df.columns)

    if missing:
        print(f"[skip] {VERTICES_FILE} missing columns: {sorted(missing)}")
        print(f"       found: {list(df.columns)}")
        return None

    if df.empty:
        print(f"[skip] {VERTICES_FILE} has no rows")
        return None

    df["algorithm"] = df["algorithm"].astype(str)
    df["vertex_id"] = pd.to_numeric(df["vertex_id"], errors="coerce")
    df["x"] = pd.to_numeric(df["x"], errors="coerce")
    df["y"] = pd.to_numeric(df["y"], errors="coerce")

    df["reached"] = pd.to_numeric(df["reached"], errors="coerce").fillna(0).astype(int)
    df["processed"] = pd.to_numeric(df["processed"], errors="coerce").fillna(0).astype(int)
    df["on_path"] = pd.to_numeric(df["on_path"], errors="coerce").fillna(0).astype(int)

    if "path_order" in df.columns:
        df["path_order"] = pd.to_numeric(df["path_order"], errors="coerce").fillna(-1).astype(int)
    else:
        df["path_order"] = -1

    df = df.dropna(subset=["vertex_id", "x", "y"])
    df["vertex_id"] = df["vertex_id"].astype(int)

    return df


def read_summary() -> Optional[pd.DataFrame]:
    if not SUMMARY_FILE.exists():
        print(f"[info] Missing {SUMMARY_FILE}; summary plot will be skipped")
        return None

    df = pd.read_csv(SUMMARY_FILE)

    if df.empty:
        print(f"[info] {SUMMARY_FILE} is empty; summary plot will be skipped")
        return None

    return df


def algorithm_label(name: str) -> str:
    low = name.lower()

    if "astar" in low or "a_star" in low or "a*" in low:
        return "A*"

    if "dijkstra" in low:
        return "Dijkstra"

    return name


def find_algorithm(df: pd.DataFrame, keywords: list[str]) -> Optional[str]:
    values = sorted(df["algorithm"].astype(str).unique())

    for value in values:
        low = value.lower()
        if any(k.lower() in low for k in keywords):
            return value

    return None


def get_src_dst(summary: Optional[pd.DataFrame]) -> tuple[int, int]:
    if summary is not None and {"src", "dst"}.issubset(summary.columns) and not summary.empty:
        src_series = pd.to_numeric(summary["src"], errors="coerce").dropna()
        dst_series = pd.to_numeric(summary["dst"], errors="coerce").dropna()

        if not src_series.empty and not dst_series.empty:
            src = int(src_series.iloc[0])
            dst = int(dst_series.iloc[0])
            return src, dst

    # Fallback if summary is missing.
    return 0, 5000


def draw_exploration_panel(
        ax,
        df_all: pd.DataFrame,
        df_alg: pd.DataFrame,
        *,
        title: str,
        src_id: int,
        dst_id: int,
        show_axes: bool = True,
) -> None:
    """
    Draw one algorithm's exploration.

    Layers:
        1. all graph vertices, light gray
        2. reached but not processed, light blue
        3. processed, orange
        4. final path, red
        5. source and destination, black/purple
    """

    # Background: all vertices for this algorithm.
    ax.scatter(
        df_alg["x"],
        df_alg["y"],
        s=0.25,
        c="#D1D5DB",
        alpha=0.35,
        linewidths=0,
        label="All vertices",
        rasterized=True,
    )

    reached = df_alg[
        (df_alg["reached"] == 1)
        & (df_alg["processed"] == 0)
        & (df_alg["on_path"] == 0)
        ]

    processed = df_alg[
        (df_alg["processed"] == 1)
        & (df_alg["on_path"] == 0)
        ]

    path = df_alg[df_alg["on_path"] == 1].copy()

    if not reached.empty:
        ax.scatter(
            reached["x"],
            reached["y"],
            s=2.0,
            c="#93C5FD",
            alpha=0.65,
            linewidths=0,
            label="Reached / discovered",
            rasterized=True,
        )

    if not processed.empty:
        ax.scatter(
            processed["x"],
            processed["y"],
            s=3.0,
            c="#F59E0B",
            alpha=0.75,
            linewidths=0,
            label="Processed",
            rasterized=True,
        )

    if not path.empty:
        ax.scatter(
            path["x"],
            path["y"],
            s=18,
            c="#DC2626",
            alpha=0.95,
            linewidths=0.25,
            edgecolors="#7F1D1D",
            label="Final path vertices",
            zorder=5,
        )

        path_ordered = path[path["path_order"] >= 0].copy()

        if len(path_ordered) >= 2:
            path_ordered = path_ordered.sort_values("path_order")

            ax.plot(
                path_ordered["x"],
                path_ordered["y"],
                color="#DC2626",
                linewidth=2.4,
                alpha=0.92,
                zorder=4,
                label="Final path",
            )
        elif len(path) >= 2:
            print(
                f"[warn] {title}: path vertices exist, but path_order is missing or invalid. "
                "Path line was not drawn."
            )

    # Mark source/destination from summary file.
    source = df_alg[df_alg["vertex_id"] == src_id]
    dest = df_alg[df_alg["vertex_id"] == dst_id]

    if not source.empty:
        ax.scatter(
            source["x"],
            source["y"],
            s=55,
            c="black",
            marker="o",
            label="Source",
            zorder=6,
        )

    if not dest.empty:
        ax.scatter(
            dest["x"],
            dest["y"],
            s=85,
            c="#7C3AED",
            marker="*",
            label="Destination",
            zorder=6,
        )

    reached_count = int(df_alg["reached"].sum())
    processed_count = int(df_alg["processed"].sum())
    path_count = int(df_alg["on_path"].sum())

    ax.set_title(
        f"{title}\nReached: {reached_count:,} | Processed: {processed_count:,} | Path vertices: {path_count:,}",
        fontsize=13,
        pad=10,
    )

    ax.set_aspect("equal", adjustable="box")

    if show_axes:
        ax.set_xlabel("x")
        ax.set_ylabel("y")
        ax.tick_params(colors="#4B5563", labelsize=8)
    else:
        ax.set_xticks([])
        ax.set_yticks([])

    for spine in ax.spines.values():
        spine.set_color("#9CA3AF")

    ax.legend(
        loc="upper right",
        frameon=True,
        facecolor="white",
        edgecolor="#D1D5DB",
        fontsize=8,
        markerscale=2,
    )


def plot_side_by_side(df: pd.DataFrame, summary: Optional[pd.DataFrame]) -> None:
    dijkstra_name = find_algorithm(df, ["dijkstra"])
    astar_name = find_algorithm(df, ["astar", "a_star", "a*"])

    if dijkstra_name is None or astar_name is None:
        print("[skip] Could not find both Dijkstra and A* algorithms")
        print(f"       algorithms found: {sorted(df['algorithm'].unique())}")
        return

    src_id, dst_id = get_src_dst(summary)

    dijkstra = df[df["algorithm"] == dijkstra_name].copy()
    astar = df[df["algorithm"] == astar_name].copy()

    fig, axes = plt.subplots(1, 2, figsize=(16, 8), facecolor="white")

    fig.suptitle(
        f"Porto Graph Exploration: Dijkstra vs A*   Source={src_id}, Destination={dst_id}",
        fontsize=17,
        fontweight="bold",
        y=0.98,
    )

    draw_exploration_panel(
        axes[0],
        df,
        dijkstra,
        title="Dijkstra",
        src_id=src_id,
        dst_id=dst_id,
        show_axes=True,
    )

    draw_exploration_panel(
        axes[1],
        df,
        astar,
        title="A*",
        src_id=src_id,
        dst_id=dst_id,
        show_axes=True,
    )

    fig.tight_layout(rect=[0, 0, 1, 0.94])

    out = PLOTS_DIR / "porto_exploration_side_by_side.png"
    fig.savefig(out, dpi=220, bbox_inches="tight", facecolor="white")
    plt.close(fig)

    print(f"[ok] {out}")


def plot_single_algorithm(
        df: pd.DataFrame,
        summary: Optional[pd.DataFrame],
        algorithm_name: str,
        filename: str,
) -> None:
    sub = df[df["algorithm"] == algorithm_name].copy()

    if sub.empty:
        return

    src_id, dst_id = get_src_dst(summary)

    fig, ax = plt.subplots(figsize=(10, 10), facecolor="white")

    draw_exploration_panel(
        ax,
        df,
        sub,
        title=algorithm_label(algorithm_name),
        src_id=src_id,
        dst_id=dst_id,
        show_axes=True,
    )

    fig.tight_layout()

    out = PLOTS_DIR / filename
    fig.savefig(out, dpi=220, bbox_inches="tight", facecolor="white")
    plt.close(fig)

    print(f"[ok] {out}")


def plot_summary(summary: Optional[pd.DataFrame]) -> None:
    if summary is None:
        return

    needed = {
        "algorithm",
        "reached_vertices",
        "processed_vertices",
        "path_vertices",
        "time_ms",
    }

    if not needed.issubset(summary.columns):
        print("[skip] Summary file missing required columns")
        print(f"       required: {sorted(needed)}")
        print(f"       found:    {list(summary.columns)}")
        return

    data = summary.copy()
    data["label"] = data["algorithm"].astype(str).map(algorithm_label)

    for col in ["reached_vertices", "processed_vertices", "path_vertices", "time_ms"]:
        data[col] = pd.to_numeric(data[col], errors="coerce")

    data = data.dropna(
        subset=[
            "reached_vertices",
            "processed_vertices",
            "path_vertices",
            "time_ms",
        ]
    )

    if data.empty:
        print("[skip] Summary data has no valid rows")
        return

    labels = data["label"].tolist()
    x = np.arange(len(labels))
    width = 0.35

    fig, ax1 = plt.subplots(figsize=(10, 6), facecolor="white")

    ax1.bar(
        x - width / 2,
        data["reached_vertices"],
        width,
        label="Reached vertices",
        color="#93C5FD",
        edgecolor="#1E3A8A",
        )

    ax1.bar(
        x + width / 2,
        data["processed_vertices"],
        width,
        label="Processed vertices",
        color="#F59E0B",
        edgecolor="#92400E",
        )

    ax1.set_ylabel("Number of vertices")
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels)
    ax1.set_title("Porto Exploration Summary", fontsize=15, fontweight="bold")
    ax1.grid(axis="y", alpha=0.25)

    ax2 = ax1.twinx()
    ax2.plot(
        x,
        data["time_ms"],
        marker="o",
        linewidth=2.5,
        color="#DC2626",
        label="Time (ms)",
    )

    ax2.set_ylabel("Time (ms)")

    handles1, labels1 = ax1.get_legend_handles_labels()
    handles2, labels2 = ax2.get_legend_handles_labels()

    ax1.legend(
        handles1 + handles2,
        labels1 + labels2,
        loc="upper right",
        frameon=True,
        facecolor="white",
        edgecolor="#D1D5DB",
        )

    fig.tight_layout()

    out = PLOTS_DIR / "porto_exploration_summary.png"
    fig.savefig(out, dpi=220, bbox_inches="tight", facecolor="white")
    plt.close(fig)

    print(f"[ok] {out}")


def main() -> None:
    setup_style()
    ensure_dirs()

    df = read_vertices()

    if df is None:
        return

    summary = read_summary()

    dijkstra_name = find_algorithm(df, ["dijkstra"])
    astar_name = find_algorithm(df, ["astar", "a_star", "a*"])

    plot_side_by_side(df, summary)

    if dijkstra_name is not None:
        plot_single_algorithm(
            df,
            summary,
            dijkstra_name,
            "porto_exploration_dijkstra.png",
        )

    if astar_name is not None:
        plot_single_algorithm(
            df,
            summary,
            astar_name,
            "porto_exploration_astar.png",
        )

    plot_summary(summary)

    print(f"\nPlots written to: {PLOTS_DIR.resolve()}")


if __name__ == "__main__":
    main()