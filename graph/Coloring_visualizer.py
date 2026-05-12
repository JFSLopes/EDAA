"""
Graph Coloring Visualizer
=========================
Reads coloring_vertices.csv and coloring_edges.csv from this directory and
writes coloring_solution.png.

Expected files:
  graph/coloring_vertices.csv: id,x,y,color
  graph/coloring_edges.csv   : x1,y1,x2,y2
"""

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patheffects as pe
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D
import pandas as pd

DIR = Path(__file__).parent
vertices_path = DIR / "coloring_vertices.csv"
edges_path = DIR / "coloring_edges.csv"
nodes_path = DIR / "nodes_clipped.csv"

if not vertices_path.exists():
    raise FileNotFoundError("coloring_vertices.csv not found — export a coloring solution first.")
if not edges_path.exists():
    raise FileNotFoundError("coloring_edges.csv not found — export a coloring solution first.")

vertices = pd.read_csv(vertices_path)
edges = pd.read_csv(edges_path)

BG = "#0D1117"
PANEL = "#161B22"
EDGE = "#9CA3AF"

PALETTE = [
    "#60A5FA",  # blue
    "#F87171",  # red
    "#34D399",  # green
    "#FBBF24",  # yellow
    "#A78BFA",  # purple
    "#FB923C",  # orange
    "#E879F9",  # pink
    "#22D3EE",  # cyan
    "#F472B6",  # rose
    "#A3E635",  # lime
]

fig, ax = plt.subplots(figsize=(16, 10), facecolor=BG)
ax.set_facecolor(PANEL)
for sp in ax.spines.values():
    sp.set_color("#21262D")

# Optional map background
if nodes_path.exists():
    nodes = pd.read_csv(nodes_path)
    if {"x", "y"}.issubset(nodes.columns) and len(nodes) < 100000:
        ax.scatter(nodes["x"], nodes["y"], s=0.2, c="#9CA3AF", alpha=0.06, linewidths=0, zorder=1)

# Conflict edges
if not edges.empty:
    segs = [[(r.x1, r.y1), (r.x2, r.y2)] for _, r in edges.iterrows()]
    lc = LineCollection(segs, colors=EDGE, linewidths=0.7, alpha=0.25, zorder=2)
    ax.add_collection(lc)

# Antennas colored by assigned channel/color
for color_id, group in vertices.groupby("color"):
    color = PALETTE[int(color_id) % len(PALETTE)]
    ax.scatter(
        group["x"], group["y"],
        s=120, c=color, edgecolors="white", linewidths=0.7,
        label=f"Color {int(color_id)}", zorder=5,
    )

    for _, row in group.iterrows():
        ax.annotate(
            str(row["id"]),
            (row["x"], row["y"]),
            textcoords="offset points",
            xytext=(6, 5),
            fontsize=7,
            color="white",
            zorder=6,
            path_effects=[pe.withStroke(linewidth=2, foreground=BG)],
        )

legend_handles = [
    Line2D([0], [0], marker="o", color="w", markerfacecolor=PALETTE[i % len(PALETTE)],
           markersize=8, label=f"Color {i}", linestyle="None")
    for i in sorted(vertices["color"].unique())
]
legend_handles.append(Line2D([0], [0], color=EDGE, lw=1.5, alpha=0.5, label="Conflict edge"))

ax.legend(handles=legend_handles, facecolor=BG, labelcolor="white", fontsize=8,
          framealpha=0.9, loc="upper right")

ax.autoscale_view()
ax.set_aspect("equal")
ax.set_title("Graph Coloring — Channel Assignment", color="white",
             fontsize=14, pad=10, fontweight="bold")
ax.tick_params(colors="#9CA3AF", labelsize=7)
ax.set_xlabel("Easting (m, UTM)", color="#9CA3AF", fontsize=8)
ax.set_ylabel("Northing (m, UTM)", color="#9CA3AF", fontsize=8)
ax.grid(color="#21262D", linewidth=0.3, alpha=0.5)

out = DIR / "coloring_solution.png"
plt.savefig(out, dpi=150, bbox_inches="tight", facecolor=BG)
plt.close()
print(f"Saved → {out}")
