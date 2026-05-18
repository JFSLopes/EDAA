from pathlib import Path
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

BASE_DIR = Path(__file__).parent.parent / "benchmark"
os.chdir(BASE_DIR)
OUT_DIR = BASE_DIR

BG, PANEL = "#0D1117", "#161B22"
GRID_C    = "#21262D"
PALETTE   = ["#60A5FA", "#F87171", "#34D399", "#FBBF24",
             "#A78BFA", "#FB923C", "#E879F9", "#22D3EE"]

def style(ax):
    ax.set_facecolor(PANEL)
    ax.tick_params(colors="#9CA3AF", labelsize=8)
    ax.xaxis.label.set_color("#9CA3AF")
    ax.yaxis.label.set_color("#9CA3AF")
    ax.title.set_color("white")
    for sp in ax.spines.values():
        sp.set_color(GRID_C)
    ax.grid(color=GRID_C, linewidth=0.4)

def savefig(fig, name):
    path = OUT_DIR / name
    fig.savefig(path, dpi=150, bbox_inches="tight", facecolor=BG)
    plt.close(fig)
    print(f"Saved -> {path}")

def load(name):
    p = BASE_DIR / name
    if not p.exists():
        print(f"  Missing: {p}")
        return pd.DataFrame()
    return pd.read_csv(p)

dij  = load("dijkstra_astar.csv")
pq   = load("priority_queues.csv")
col  = load("coloring.csv")
quad = load("quadtree.csv")
prim = load("prim.csv")

# ══════════════════════════════════════════════════════════════════════════════
# Plot 1 — Dijkstra vs A*
#   Rows: variant × n × param_secondary (avg_degree)
#   Two panels per degree value: time vs n, A* speedup vs n
# ══════════════════════════════════════════════════════════════════════════════
if not dij.empty:
    degrees = sorted(dij["param_secondary"].unique())

    # 1a — one figure with subplots per degree: time lines
    fig, axes = plt.subplots(1, len(degrees), figsize=(5 * len(degrees), 5),
                             facecolor=BG, sharey=False)
    if len(degrees) == 1:
        axes = [axes]

    for ax, deg in zip(axes, degrees):
        sub = dij[dij["param_secondary"] == deg]
        for i, (name, grp) in enumerate(sub.groupby("variant")):
            ok = grp[grp["dnf"] == 0].sort_values("n")
            dnf = grp[grp["dnf"] == 1]
            ax.plot(ok["n"], ok["elapsed_s"], marker="o", color=PALETTE[i],
                    label=name, linewidth=1.8, markersize=4)
            if not dnf.empty:
                ax.scatter(dnf["n"], dnf["elapsed_s"], marker="x",
                           color=PALETTE[i], s=60, zorder=5)
        style(ax)
        ax.set_title(f"deg={int(deg)}")
        ax.set_xlabel("Vertices (n)")
        ax.set_ylabel("Avg elapsed (s)")
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.legend(facecolor=PANEL, labelcolor="white", fontsize=7)

    fig.suptitle("Dijkstra vs A* — Time vs Graph Size (per avg degree)",
                 color="white", fontsize=13)
    savefig(fig, "plot_dijkstra_time.png")

    # 1b — A* speedup per degree
    fig, axes = plt.subplots(1, len(degrees), figsize=(5 * len(degrees), 5),
                             facecolor=BG, sharey=True)
    if len(degrees) == 1:
        axes = [axes]

    for ax, deg in zip(axes, degrees):
        sub = dij[(dij["param_secondary"] == deg) & (dij["dnf"] == 0)]
        for color, dij_var, astar_var, pq_label in [
            (PALETTE[0], "Dijkstra_FibHeap",  "AStar_FibHeap",  "FibHeap"),
            (PALETTE[1], "Dijkstra_BinHeap",  "AStar_BinHeap",  "BinHeap"),
        ]:
            d = sub[sub["variant"] == dij_var].set_index("n")["elapsed_s"]
            a = sub[sub["variant"] == astar_var].set_index("n")["elapsed_s"]
            common = d.index.intersection(a.index)
            if len(common) == 0:
                continue
            speedup = d[common] / a[common]
            ax.plot(common, speedup, marker="s", color=color,
                    label=f"A* ({pq_label})", linewidth=1.8, markersize=4)
        ax.axhline(1.0, color="white", linewidth=0.6, linestyle="--", alpha=0.4)
        style(ax)
        ax.set_title(f"Speedup — deg={int(deg)}")
        ax.set_xlabel("Vertices (n)")
        ax.set_ylabel("Speedup (×)")
        ax.set_xscale("log")
        ax.legend(facecolor=PANEL, labelcolor="white", fontsize=7)

    fig.suptitle("A* Speedup over Dijkstra (per avg degree)",
                 color="white", fontsize=13)
    savefig(fig, "plot_dijkstra_speedup.png")

# ══════════════════════════════════════════════════════════════════════════════
# Plot 2 — Priority Queue comparison: BruteForce vs FibHeap
#   Param_secondary = avg_degree
# ══════════════════════════════════════════════════════════════════════════════
if not pq.empty:
    degrees = sorted(pq["param_secondary"].unique())
    fig, axes = plt.subplots(1, len(degrees), figsize=(5 * len(degrees), 5),
                             facecolor=BG, sharey=False)
    if len(degrees) == 1:
        axes = [axes]

    for ax, deg in zip(axes, degrees):
        sub = pq[(pq["param_secondary"] == deg) & (pq["dnf"] == 0)]
        for i, (name, grp) in enumerate(sub.groupby("variant")):
            grp = grp.sort_values("n")
            ax.plot(grp["n"], grp["elapsed_s"], marker="o", color=PALETTE[i],
                    label=name, linewidth=1.8, markersize=4)
        style(ax)
        ax.set_title(f"deg={int(deg)}")
        ax.set_xlabel("Vertices (n)")
        ax.set_ylabel("Avg elapsed (s)")
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

    fig.suptitle("Priority Queue Comparison — BruteForce vs FibHeap",
                 color="white", fontsize=13)
    savefig(fig, "plot_priority_queues.png")

# ══════════════════════════════════════════════════════════════════════════════
# Plot 3 — Graph Coloring
#   3a — Random instances: time and colors-used per radius
#   3b — Hard instances: time comparison table + bar chart
#   3c — Suboptimality heatmap (WP − BF) for random instances
# ══════════════════════════════════════════════════════════════════════════════
if not col.empty:

    # Separate random vs hard variants
    hard_prefixes = ("WelshPowell_Hard", "BruteForce_Hard")
    rand_mask = ~col["variant"].str.startswith(("WelshPowell_Hard", "BruteForce_Hard"))
    col_rand = col[rand_mask].copy()
    col_hard = col[~rand_mask].copy()

    # --- 3a: random instances ------------------------------------------------
    if not col_rand.empty:
        radii   = sorted(col_rand["param_secondary"].unique())
        n_radii = len(radii)

        fig, axes = plt.subplots(2, n_radii, figsize=(5 * n_radii, 10), facecolor=BG)
        if n_radii == 1:
            axes = axes.reshape(2, 1)

        for ri, radius in enumerate(radii):
            sub = col_rand[col_rand["param_secondary"] == radius]

            # Time (top row) — include DNF rows with different marker
            ax = axes[0, ri]
            for i, (name, grp) in enumerate(sub.groupby("variant")):
                ok  = grp[grp["dnf"] == 0].sort_values("n")
                dnf = grp[grp["dnf"] == 1].sort_values("n")
                ax.plot(ok["n"], ok["elapsed_s"], marker="o", color=PALETTE[i],
                        label=name, linewidth=1.8, markersize=4)
                if not dnf.empty:
                    ax.scatter(dnf["n"], dnf["elapsed_s"], marker="X",
                               color=PALETTE[i], s=80, zorder=5,
                               label=f"{name} (DNF)")
            style(ax)
            ax.set_title(f"Time — radius={int(radius)}m", fontsize=10)
            ax.set_xlabel("Antennas (n)")
            ax.set_ylabel("Elapsed (s)")
            ax.set_yscale("log")
            ax.legend(facecolor=PANEL, labelcolor="white", fontsize=7)

            # Colors used (bottom row) — only non-DNF
            ax = axes[1, ri]
            for i, (name, grp) in enumerate(sub.groupby("variant")):
                grp = grp[(grp["dnf"] == 0) & (grp["quality"] > 0)].sort_values("n")
                ax.plot(grp["n"], grp["quality"], marker="s", color=PALETTE[i],
                        label=name, linewidth=1.8, markersize=4)
            style(ax)
            ax.set_title(f"Colors Used — radius={int(radius)}m", fontsize=10)
            ax.set_xlabel("Antennas (n)")
            ax.set_ylabel("Colors used")
            ax.legend(facecolor=PANEL, labelcolor="white", fontsize=7)

        fig.suptitle("Graph Coloring — Random Instances",
                     color="white", fontsize=13, y=1.01)
        fig.tight_layout()
        savefig(fig, "plot_coloring_random.png")

        # Suboptimality heatmap
        wp = col_rand[col_rand["variant"] == "WelshPowell"]
        bf = col_rand[(col_rand["variant"] == "BruteForce") & (col_rand["dnf"] == 0)]
        if not wp.empty and not bf.empty:
            merged = wp.merge(bf, on=["n", "param_secondary"], suffixes=("_wp", "_bf"))
            merged["gap"] = merged["quality_wp"] - merged["quality_bf"]
            pivot = merged.pivot(index="n", columns="param_secondary", values="gap")

            fig, ax = plt.subplots(figsize=(8, 5), facecolor=BG)
            im = ax.imshow(pivot.values, aspect="auto", cmap="RdYlGn_r",
                           origin="lower", vmin=0)
            ax.set_xticks(range(len(pivot.columns)))
            ax.set_xticklabels([f"{int(r)}m" for r in pivot.columns],
                               color="#9CA3AF", fontsize=8)
            ax.set_yticks(range(len(pivot.index)))
            ax.set_yticklabels(pivot.index.tolist(), color="#9CA3AF", fontsize=8)
            ax.set_xlabel("Interference radius", color="#9CA3AF")
            ax.set_ylabel("Antennas (n)", color="#9CA3AF")
            ax.set_title("Suboptimality gap: WelshPowell − Exact (0 = optimal)",
                         color="white", fontsize=11)
            for sp in ax.spines.values():
                sp.set_color(GRID_C)
            for i in range(len(pivot.index)):
                for j in range(len(pivot.columns)):
                    val = pivot.values[i, j]
                    if not np.isnan(val):
                        ax.text(j, i, str(int(val)), ha="center", va="center",
                                color="white", fontsize=8, fontweight="bold")
            cb = plt.colorbar(im, ax=ax)
            cb.ax.tick_params(labelcolor="#9CA3AF", labelsize=7)
            cb.set_label("Extra colors vs optimal", color="#9CA3AF", fontsize=8)
            fig.suptitle("Welsh-Powell Suboptimality Heatmap",
                         color="white", fontsize=13)
            savefig(fig, "plot_coloring_gap.png")

    # --- 3b: hard instances --------------------------------------------------
    if not col_hard.empty:
        instance_types = col_hard["variant"].str.extract(r"Hard_(.+)$")[0].unique()
        n_types = len(instance_types)

        fig, axes = plt.subplots(1, n_types, figsize=(6 * n_types, 5), facecolor=BG)
        if n_types == 1:
            axes = [axes]

        for ax, itype in zip(axes, instance_types):
            sub = col_hard[col_hard["variant"].str.endswith(itype)]
            for i, (name, grp) in enumerate(sub.groupby("variant")):
                grp_sorted = grp.sort_values("n")
                ok  = grp_sorted[grp_sorted["dnf"] == 0]
                dnf = grp_sorted[grp_sorted["dnf"] == 1]
                ax.plot(ok["n"], ok["elapsed_s"], marker="o", color=PALETTE[i],
                        label=name.replace(f"_Hard_{itype}", ""), linewidth=1.8, markersize=4)
                if not dnf.empty:
                    ax.scatter(dnf["n"], dnf["elapsed_s"], marker="X",
                               color=PALETTE[i], s=80, zorder=5)
                    # Annotate DNF
                    for _, row in dnf.iterrows():
                        ax.annotate("DNF", (row["n"], row["elapsed_s"]),
                                    textcoords="offset points", xytext=(4, 4),
                                    color="#9CA3AF", fontsize=7)
            style(ax)
            ax.set_title(f"Hard — {itype}", fontsize=10)
            ax.set_xlabel("Antennas (n)")
            ax.set_ylabel("Elapsed (s)")
            ax.set_yscale("log")
            ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

        fig.suptitle("Graph Coloring — Hard Instances (Dense + Petersen-like)",
                     color="white", fontsize=13)
        savefig(fig, "plot_coloring_hard.png")

# ══════════════════════════════════════════════════════════════════════════════
# Plot 4 — Quadtree vs BruteForce: connectivity sweep
#   param_secondary = average degree / connectivity
#   elapsed_s = total graph construction time
#   quality = metric selected by quality_label
# ══════════════════════════════════════════════════════════════════════════════
if not quad.empty:

    # ── Use only the main distance-check rows for time plots ────────────────
    quad_checks = quad[quad["quality_label"] == "distance_checks"].copy()
    quad_cps    = quad[quad["quality_label"] == "checks_per_second"].copy()

    if not quad_checks.empty:
        node_counts = sorted(quad_checks["n"].unique())

        # ── 4a: Time vs average degree ─────────────────────────────────────
        fig, axes = plt.subplots(1, len(node_counts),
                                 figsize=(6 * len(node_counts), 5),
                                 facecolor=BG, sharey=False)

        if len(node_counts) == 1:
            axes = [axes]

        for ai, N in enumerate(node_counts):
            sub = quad_checks[quad_checks["n"] == N]

            qt = sub[sub["variant"] == "Quadtree"].sort_values("param_secondary")
            bf = sub[sub["variant"] == "BruteForce"].sort_values("param_secondary")

            ax = axes[ai]

            ax.plot(qt["param_secondary"], qt["elapsed_s"],
                    marker="o", color=PALETTE[0],
                    label="Quadtree", linewidth=1.8, markersize=4)

            ax.plot(bf["param_secondary"], bf["elapsed_s"],
                    marker="s", color=PALETTE[1],
                    label="BruteForce", linewidth=1.8, markersize=4)

            style(ax)
            ax.set_title(f"Total time — N={N}", fontsize=10)
            ax.set_xlabel("Average degree / connectivity")
            ax.set_ylabel("Elapsed time (s)")
            ax.set_yscale("log")
            ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

        fig.suptitle("Quadtree vs BruteForce — Time vs Connectivity",
                     color="white", fontsize=13)
        fig.tight_layout()
        savefig(fig, "plot_quadtree_time_vs_connectivity.png")

        # ── 4b: Distance checks vs average degree ──────────────────────────
        fig, axes = plt.subplots(1, len(node_counts),
                                 figsize=(6 * len(node_counts), 5),
                                 facecolor=BG, sharey=False)

        if len(node_counts) == 1:
            axes = [axes]

        for ai, N in enumerate(node_counts):
            sub = quad_checks[quad_checks["n"] == N]

            qt = sub[sub["variant"] == "Quadtree"].sort_values("param_secondary")
            bf = sub[sub["variant"] == "BruteForce"].sort_values("param_secondary")

            ax = axes[ai]

            ax.plot(qt["param_secondary"], qt["quality"],
                    marker="o", color=PALETTE[0],
                    label="Quadtree checks", linewidth=1.8, markersize=4)

            ax.plot(bf["param_secondary"], bf["quality"],
                    marker="s", color=PALETTE[1],
                    label="BruteForce checks", linewidth=1.8, markersize=4)

            style(ax)
            ax.set_title(f"Distance checks — N={N}", fontsize=10)
            ax.set_xlabel("Average degree / connectivity")
            ax.set_ylabel("Distance checks")
            ax.set_yscale("log")
            ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

        fig.suptitle("Quadtree vs BruteForce — Checks vs Connectivity",
                     color="white", fontsize=13)
        fig.tight_layout()
        savefig(fig, "plot_quadtree_checks_vs_connectivity.png")

        # ── 4c: Runtime speedup vs average degree ──────────────────────────
        fig, axes = plt.subplots(1, len(node_counts),
                                 figsize=(6 * len(node_counts), 5),
                                 facecolor=BG, sharey=False)

        if len(node_counts) == 1:
            axes = [axes]

        for ai, N in enumerate(node_counts):
            sub = quad_checks[quad_checks["n"] == N]

            qt = sub[sub["variant"] == "Quadtree"].set_index("param_secondary").sort_index()
            bf = sub[sub["variant"] == "BruteForce"].set_index("param_secondary").sort_index()

            common = qt.index.intersection(bf.index)

            ax = axes[ai]

            if len(common) > 0:
                speedup = bf.loc[common, "elapsed_s"] / qt.loc[common, "elapsed_s"]

                ax.plot(common, speedup,
                        marker="D", color=PALETTE[2],
                        label="BF time / QT time",
                        linewidth=1.8, markersize=5)

                ax.axhline(1.0, color="white", linewidth=0.6,
                           linestyle="--", alpha=0.5)

                slower = speedup[speedup < 1.0]
                if not slower.empty:
                    first_x = slower.index[0]
                    first_y = slower.iloc[0]
                    ax.scatter([first_x], [first_y],
                               marker="X", s=90, color=PALETTE[1],
                               zorder=5, label="Breaking point")
                    ax.annotate(f"break ≈ {first_x:.1f}",
                                (first_x, first_y),
                                textcoords="offset points",
                                xytext=(6, 6),
                                color="#9CA3AF",
                                fontsize=8)

            style(ax)
            ax.set_title(f"Runtime speedup — N={N}", fontsize=10)
            ax.set_xlabel("Average degree / connectivity")
            ax.set_ylabel("Speedup ×")
            ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

        fig.suptitle("Quadtree Runtime Speedup vs Connectivity",
                     color="white", fontsize=13)
        fig.tight_layout()
        savefig(fig, "plot_quadtree_speedup_vs_connectivity.png")

        # ── 4d: Check reduction vs average degree ──────────────────────────
        fig, axes = plt.subplots(1, len(node_counts),
                                 figsize=(6 * len(node_counts), 5),
                                 facecolor=BG, sharey=False)

        if len(node_counts) == 1:
            axes = [axes]

        for ai, N in enumerate(node_counts):
            sub = quad_checks[quad_checks["n"] == N]

            qt = sub[sub["variant"] == "Quadtree"].set_index("param_secondary").sort_index()
            bf = sub[sub["variant"] == "BruteForce"].set_index("param_secondary").sort_index()

            common = qt.index.intersection(bf.index)

            ax = axes[ai]

            if len(common) > 0:
                reduction = bf.loc[common, "quality"] / qt.loc[common, "quality"]

                ax.plot(common, reduction,
                        marker="D", color=PALETTE[3],
                        label="BF checks / QT checks",
                        linewidth=1.8, markersize=5)

                ax.axhline(1.0, color="white", linewidth=0.6,
                           linestyle="--", alpha=0.5)

            style(ax)
            ax.set_title(f"Check reduction — N={N}", fontsize=10)
            ax.set_xlabel("Average degree / connectivity")
            ax.set_ylabel("Reduction factor ×")
            ax.set_yscale("log")
            ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

        fig.suptitle("Quadtree Check Reduction vs Connectivity",
                     color="white", fontsize=13)
        fig.tight_layout()
        savefig(fig, "plot_quadtree_check_reduction_vs_connectivity.png")

    # ── 4e: Checks per second vs average degree ─────────────────────────────
    if not quad_cps.empty:
        node_counts = sorted(quad_cps["n"].unique())

        fig, axes = plt.subplots(1, len(node_counts),
                                 figsize=(6 * len(node_counts), 5),
                                 facecolor=BG, sharey=False)

        if len(node_counts) == 1:
            axes = [axes]

        for ai, N in enumerate(node_counts):
            sub = quad_cps[quad_cps["n"] == N]

            qt = sub[sub["variant"] == "Quadtree"].sort_values("param_secondary")
            bf = sub[sub["variant"] == "BruteForce"].sort_values("param_secondary")

            ax = axes[ai]

            ax.plot(qt["param_secondary"], qt["quality"],
                    marker="o", color=PALETTE[0],
                    label="Quadtree checks/s", linewidth=1.8, markersize=4)

            ax.plot(bf["param_secondary"], bf["quality"],
                    marker="s", color=PALETTE[1],
                    label="BruteForce checks/s", linewidth=1.8, markersize=4)

            style(ax)
            ax.set_title(f"Checks per second — N={N}", fontsize=10)
            ax.set_xlabel("Average degree / connectivity")
            ax.set_ylabel("Checks per second")
            ax.set_yscale("log")
            ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

        fig.suptitle("Quadtree vs BruteForce — Checks per Second",
                     color="white", fontsize=13)
        fig.tight_layout()
        savefig(fig, "plot_quadtree_checks_per_second.png")


# ══════════════════════════════════════════════════════════════════════════════
# Plot — Prim: FibonacciHeap vs MutablePriorityQueue
#   param_secondary = avg_degree
#   elapsed_s = time to compute MST
#   quality = MST total weight
# ══════════════════════════════════════════════════════════════════════════════
if not prim.empty:
    degrees = sorted(prim["param_secondary"].unique())

    # ── Prim time vs graph size ─────────────────────────────────────────────
    fig, axes = plt.subplots(1, len(degrees), figsize=(5 * len(degrees), 5),
                             facecolor=BG, sharey=False)

    if len(degrees) == 1:
        axes = [axes]

    for ax, deg in zip(axes, degrees):
        sub = prim[(prim["param_secondary"] == deg) & (prim["dnf"] == 0)]

        for i, (name, grp) in enumerate(sub.groupby("variant")):
            grp = grp.sort_values("n")

            ax.plot(grp["n"], grp["elapsed_s"],
                    marker="o",
                    color=PALETTE[i],
                    label=name,
                    linewidth=1.8,
                    markersize=4)

        style(ax)
        ax.set_title(f"avg_degree={int(deg)}", fontsize=10)
        ax.set_xlabel("Vertices (n)")
        ax.set_ylabel("Elapsed time (s)")
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

    fig.suptitle("Prim — FibonacciHeap vs MutablePriorityQueue",
                 color="white", fontsize=13)

    fig.tight_layout()
    savefig(fig, "plot_prim_time.png")

    # ── Prim speedup ────────────────────────────────────────────────────────
    fig, axes = plt.subplots(1, len(degrees), figsize=(5 * len(degrees), 5),
                             facecolor=BG, sharey=False)

    if len(degrees) == 1:
        axes = [axes]

    for ax, deg in zip(axes, degrees):
        sub = prim[(prim["param_secondary"] == deg) & (prim["dnf"] == 0)]

        fib = sub[sub["variant"] == "Prim_FibHeap"].set_index("n").sort_index()
        mut = sub[sub["variant"] == "Prim_MutablePQ"].set_index("n").sort_index()

        common = fib.index.intersection(mut.index)

        if len(common) > 0:
            # > 1 means FibonacciHeap is faster than MutablePQ.
            speedup = mut.loc[common, "elapsed_s"] / fib.loc[common, "elapsed_s"]

            ax.plot(common, speedup,
                    marker="D",
                    color=PALETTE[2],
                    label="MutablePQ time / FibHeap time",
                    linewidth=1.8,
                    markersize=5)

            ax.axhline(1.0,
                       color="white",
                       linewidth=0.6,
                       linestyle="--",
                       alpha=0.5)

        style(ax)
        ax.set_title(f"Speedup — avg_degree={int(deg)}", fontsize=10)
        ax.set_xlabel("Vertices (n)")
        ax.set_ylabel("Speedup ×")
        ax.set_xscale("log")
        ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

    fig.suptitle("Prim — FibonacciHeap Speedup over MutablePriorityQueue",
                 color="white", fontsize=13)

    fig.tight_layout()
    savefig(fig, "plot_prim_speedup.png")

    # ── MST weight correctness comparison ──────────────────────────────────
    fig, axes = plt.subplots(1, len(degrees), figsize=(5 * len(degrees), 5),
                             facecolor=BG, sharey=False)

    if len(degrees) == 1:
        axes = [axes]

    for ax, deg in zip(axes, degrees):
        sub = prim[(prim["param_secondary"] == deg) & (prim["dnf"] == 0)]

        for i, (name, grp) in enumerate(sub.groupby("variant")):
            grp = grp.sort_values("n")

            ax.plot(grp["n"], grp["quality"],
                    marker="s",
                    color=PALETTE[i],
                    label=name,
                    linewidth=1.8,
                    markersize=4)

        style(ax)
        ax.set_title(f"MST weight — avg_degree={int(deg)}", fontsize=10)
        ax.set_xlabel("Vertices (n)")
        ax.set_ylabel("MST total weight")
        ax.set_xscale("log")
        ax.legend(facecolor=PANEL, labelcolor="white", fontsize=8)

    fig.suptitle("Prim — MST Weight Consistency",
                 color="white", fontsize=13)

    fig.tight_layout()
    savefig(fig, "plot_prim_mst_weight.png")

# ══════════════════════════════════════════════════════════════════════════════
# Summary page
# ══════════════════════════════════════════════════════════════════════════════
fig, axes = plt.subplots(2, 3, figsize=(22, 10), facecolor=BG)
axes = axes.flatten()
panels = [
    ("Dijkstra vs A* (FibHeap, deg=10)",
     dij[(dij["param_secondary"] == 10) & (dij["dnf"] == 0)] if not dij.empty else pd.DataFrame()),
    ("PQ Comparison (deg=10)",
     pq[(pq["param_secondary"] == 10) & (pq["dnf"] == 0)] if not pq.empty else pd.DataFrame()),
    ("Prim MST (deg=8)",
     prim[(prim["param_secondary"] == 8) & (prim["dnf"] == 0)] if not prim.empty else pd.DataFrame()),
    ("Coloring (radius=1000m)",
     col[(col["param_secondary"] == 1000.0) &
         ~col["variant"].str.contains("Hard")] if not col.empty else pd.DataFrame()),
    ("Quadtree interference graph (radius=1000m)",
     quad[quad["param_secondary"] == 1000.0] if not quad.empty else pd.DataFrame()),
]

for ax, (title, df) in zip(axes, panels):
    if df.empty:
        ax.text(0.5, 0.5, "No data", ha="center", va="center",
                color="#9CA3AF", transform=ax.transAxes)
        style(ax); ax.set_title(title); continue
    for i, (name, grp) in enumerate(df.groupby("variant")):
        grp = grp.sort_values("n")
        ax.plot(grp["n"], grp["elapsed_s"], marker="o", color=PALETTE[i],
                label=name, linewidth=1.5, markersize=3)
    style(ax)
    ax.set_title(title, fontsize=10)
    ax.set_xlabel("n")
    ax.set_ylabel("elapsed (s)")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.legend(facecolor=PANEL, labelcolor="white", fontsize=7)

fig.suptitle("EDAA Benchmark Summary", color="white", fontsize=15, fontweight="bold")
savefig(fig, "plot_summary.png")

print("\nAll plots generated.")