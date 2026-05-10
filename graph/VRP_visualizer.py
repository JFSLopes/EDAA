"""
VRP Solution Visualiser
========================
Reads vrp_routes.csv and vrp_stops.csv from the same directory and produces
vrp_solution.png showing:
  · Background walk-network nodes (faint)
  · One colour per bus — route edges drawn as arrows
  · Depot markers (stars)
  · Pickup markers (triangles up) with people count
  · Dropoff markers (triangles down) with people count
  · Per-bus load profile (bar chart inset)
"""

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.patheffects as pe
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D
from pathlib import Path
import numpy as np

DIR = Path(__file__).parent

# ── Theme ─────────────────────────────────────────────────────────────────────
BG, PANEL = "#0D1117", "#161B22"

# One colour per bus — extend if you have more than 8 buses
BUS_PALETTE = [
    "#60A5FA",  # blue
    "#F87171",  # red
    "#34D399",  # green
    "#FBBF24",  # yellow
    "#A78BFA",  # purple
    "#FB923C",  # orange
    "#E879F9",  # pink
    "#22D3EE",  # cyan
]

# ── Load data ─────────────────────────────────────────────────────────────────
routes_path = DIR / "vrp_routes.csv"
stops_path  = DIR / "vrp_stops.csv"
nodes_path  = DIR / "nodes_clipped.csv"

if not routes_path.exists() or not stops_path.exists():
    raise FileNotFoundError(
        "vrp_routes.csv or vrp_stops.csv not found — run the VRP solver first."
    )

routes = pd.read_csv(routes_path)
stops  = pd.read_csv(stops_path)

bus_ids   = list(routes["bus_id"].unique())
color_map = {bid: BUS_PALETTE[i % len(BUS_PALETTE)] for i, bid in enumerate(bus_ids)}

# ── Figure layout ─────────────────────────────────────────────────────────────
n_buses  = len(bus_ids)
fig      = plt.figure(figsize=(18, 10 + n_buses * 1.2), facecolor=BG)

# Main map on the left, load profiles stacked on the right
gs = fig.add_gridspec(
    n_buses, 2,
    width_ratios=[2.2, 1],
    hspace=0.55, wspace=0.28,
    left=0.04, right=0.97, top=0.93, bottom=0.04
)

ax_map = fig.add_subplot(gs[:, 0])
ax_map.set_facecolor(PANEL)
for sp in ax_map.spines.values():
    sp.set_color("#21262D")

load_axes = [fig.add_subplot(gs[i, 1]) for i in range(n_buses)]
for ax in load_axes:
    ax.set_facecolor(PANEL)
    for sp in ax.spines.values():
        sp.set_color("#21262D")

# ── Background nodes ──────────────────────────────────────────────────────────
if nodes_path.exists():
    nodes = pd.read_csv(nodes_path)
    if len(nodes) < 80000:
        ax_map.scatter(nodes["x"], nodes["y"],
                       s=0.2, c="#34D399", alpha=0.08, linewidths=0, zorder=1)

# ── Draw routes (per bus) ─────────────────────────────────────────────────────
for bus_id in bus_ids:
    color   = color_map[bus_id]
    bus_routes = routes[routes["bus_id"] == bus_id]

    segs = [
        [(r.x1, r.y1), (r.x2, r.y2)]
        for _, r in bus_routes.iterrows()
    ]
    if segs:
        lc = LineCollection(segs, colors=color, linewidths=1.8,
                            alpha=0.75, zorder=2)
        ax_map.add_collection(lc)

    # Draw arrowheads at midpoint of each segment
    for _, r in bus_routes.iterrows():
        mx, my = (r.x1 + r.x2) / 2, (r.y1 + r.y2) / 2
        dx, dy = r.x2 - r.x1, r.y2 - r.y1
        norm   = np.hypot(dx, dy)
        if norm > 0:
            ax_map.annotate(
                "", xy=(mx + dx/norm*30, my + dy/norm*30),
                xytext=(mx - dx/norm*30, my - dy/norm*30),
                arrowprops=dict(arrowstyle="-|>", color=color,
                                lw=1.2, mutation_scale=10),
                zorder=3
            )

# ── Draw stops ────────────────────────────────────────────────────────────────
for bus_id in bus_ids:
    color      = color_map[bus_id]
    bus_stops  = stops[stops["bus_id"] == bus_id]

    for _, row in bus_stops.iterrows():
        x, y = row["x"], row["y"]

        if row["type"] == "depot" or row["type"] == "depot_return":
            ax_map.scatter(x, y, s=220, c=color, marker="*",
                           zorder=6, edgecolors="white", linewidths=0.5)
            ax_map.annotate(
                str(row["label"]),
                (x, y), textcoords="offset points", xytext=(6, 6),
                fontsize=6.5, color="white", zorder=7,
                path_effects=[pe.withStroke(linewidth=1.5, foreground=BG)]
            )

        elif row["type"] == "pickup":
            ax_map.scatter(x, y, s=120, c=color, marker="^",
                           zorder=5, edgecolors="white", linewidths=0.5)
            ax_map.annotate(
                f"+{int(row['people_delta'])}",
                (x, y), textcoords="offset points", xytext=(5, 5),
                fontsize=7, color=color, fontweight="bold", zorder=7,
                path_effects=[pe.withStroke(linewidth=1.5, foreground=BG)]
            )

        elif row["type"] == "dropoff":
            ax_map.scatter(x, y, s=120, c=color, marker="v",
                           zorder=5, edgecolors="white", linewidths=0.5)
            ax_map.annotate(
                f"{int(row['people_delta'])}",   # already negative
                (x, y), textcoords="offset points", xytext=(5, -12),
                fontsize=7, color=color, fontweight="bold", zorder=7,
                path_effects=[pe.withStroke(linewidth=1.5, foreground=BG)]
            )

# ── Map cosmetics ─────────────────────────────────────────────────────────────
ax_map.autoscale_view()
ax_map.set_aspect("equal")
ax_map.set_title("VRP Solution — Bus Routes", color="white",
                 fontsize=14, pad=10, fontweight="bold")
ax_map.tick_params(colors="#9CA3AF", labelsize=7)
ax_map.set_xlabel("Easting (m, UTM)", color="#9CA3AF", fontsize=8)
ax_map.set_ylabel("Northing (m, UTM)", color="#9CA3AF", fontsize=8)

# Legend
legend_handles = [
                     mpatches.Patch(color=color_map[bid], label=bid) for bid in bus_ids
                 ] + [
                     Line2D([0],[0], marker="*",  color="w", markerfacecolor="gray",
                            markersize=9, label="Depot",   linestyle="None"),
                     Line2D([0],[0], marker="^",  color="w", markerfacecolor="gray",
                            markersize=8, label="Pickup",  linestyle="None"),
                     Line2D([0],[0], marker="v",  color="w", markerfacecolor="gray",
                            markersize=8, label="Dropoff", linestyle="None"),
                 ]
ax_map.legend(handles=legend_handles, facecolor="#0D1117", labelcolor="white",
              fontsize=8, framealpha=0.9, loc="upper right")

# ── Load profile per bus ──────────────────────────────────────────────────────
for i, bus_id in enumerate(bus_ids):
    ax    = load_axes[i]
    color = color_map[bus_id]

    bus_stops = stops[stops["bus_id"] == bus_id].copy()
    bus_stops = bus_stops[bus_stops["type"].isin(["pickup", "dropoff"])].reset_index(drop=True)

    if bus_stops.empty:
        ax.set_title(f"{bus_id} — no stops", color=color, fontsize=9)
        ax.axis("off")
        continue

    # Capacity from the first non-zero load_after
    capacity = stops[stops["bus_id"] == bus_id]["load_after"].max()

    labels   = [f"#{j+1}\n{r['type'][:4]}\n{r['request_id']}"
                for j, (_, r) in enumerate(bus_stops.iterrows())]
    loads    = bus_stops["load_after"].tolist()
    deltas   = bus_stops["people_delta"].tolist()
    xs       = range(len(loads))

    bars = ax.bar(xs, loads, color=color, alpha=0.75,
                  edgecolor=BG, linewidth=0.5)

    # Capacity line
    ax.axhline(capacity, color="white", linewidth=0.8,
               linestyle="--", alpha=0.5, label=f"capacity={int(capacity)}")

    # Delta annotation on each bar
    for j, (bar, delta, load) in enumerate(zip(bars, deltas, loads)):
        sign   = "+" if delta > 0 else ""
        ax.text(bar.get_x() + bar.get_width()/2,
                load + capacity * 0.02,
                f"{sign}{int(delta)}",
                ha="center", va="bottom", fontsize=7,
                color="white",
                path_effects=[pe.withStroke(linewidth=1, foreground=BG)])

    ax.set_xticks(list(xs))
    ax.set_xticklabels(labels, fontsize=6.5, color="#9CA3AF")
    ax.set_ylim(0, capacity * 1.25)
    ax.set_ylabel("Passengers", color="#9CA3AF", fontsize=7)
    ax.tick_params(axis="y", colors="#9CA3AF", labelsize=7)
    ax.set_title(f"{bus_id}  (cap={int(capacity)})", color=color,
                 fontsize=9, fontweight="bold", pad=4)
    ax.legend(facecolor=PANEL, labelcolor="white", fontsize=7,
              loc="upper right", framealpha=0.7)
    ax.grid(axis="y", color="#21262D", linewidth=0.4)

# ── Save ──────────────────────────────────────────────────────────────────────
out = DIR / "vrp_solution.png"
plt.savefig(out, dpi=150, bbox_inches="tight", facecolor=BG)
plt.close()
print(f"Saved → {out}")