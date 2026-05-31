"""
Porto Graph — Clip Transit Nodes to Walk Area
=============================================
Loads nodes_norm.csv and edges_norm.csv (output of porto_graph.py) and
removes any bus/metro nodes (and their edges) that fall outside the
convex hull of the walk layer nodes.

Why convex hull instead of bounding box?
  A bounding box is axis-aligned and includes corners that may be far
  outside the actual walk coverage. The convex hull is the tightest
  possible polygon around the walk nodes, so it correctly excludes
  transit stops in neighbouring municipalities that leaked in via GTFS
  but have no walkable connection to Porto.

Outputs
  nodes_clipped.csv   — filtered canonical nodes
  edges_clipped.csv   — filtered edges (any edge touching a removed node is dropped)
  graph_clipped.png   — visual comparison: before vs after clipping
"""

import numpy as np
import pandas as pd
from scipy.spatial import ConvexHull, cKDTree
import networkx as nx
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D
from matplotlib.patches import Polygon as MplPolygon

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════

INPUT_NODES = "nodes_norm.csv"
INPUT_EDGES = "edges_norm.csv"
OUTPUT_NODES = "nodes_clipped.csv"
OUTPUT_EDGES = "edges_clipped.csv"

# How to define "inside the walk area" — choose one:
#   "convex_hull"  — tightest polygon around walk nodes (recommended)
#   "bbox"         — simple axis-aligned bounding box (more permissive)
CLIP_METHOD = "convex_hull"

# Optional padding in metres — expands the clipping boundary outward so
# transit stops just outside the walk hull (e.g. at a terminus) are kept.
PADDING_M = 50

# Plot theme
BG, PANEL, GRID_C = "#0D1117", "#161B22", "#21262D"
C_WALK, C_BUS, C_METRO = "#34D399", "#60A5FA", "#F87171"
C_TRANSFER, C_XCHANGE  = "#FBBF24", "#E879F9"
C_REMOVED              = "#EF4444"

# ═══════════════════════════════════════════════════════════════════════════════
# § 1 · Load data
# ═══════════════════════════════════════════════════════════════════════════════
print("== 1 · Loading data ====================================================")

nodes = pd.read_csv(INPUT_NODES, dtype={"id": str, "name": str})
edges = pd.read_csv(INPUT_EDGES, dtype={"source": str, "target": str, "mode": str})
nodes["name"] = nodes["name"].fillna("").astype(str)

print(f"  Nodes: {len(nodes):,}   Edges: {len(edges):,}")

# ═══════════════════════════════════════════════════════════════════════════════
# § 2 · Build walk-area boundary
# ═══════════════════════════════════════════════════════════════════════════════
print(f"\n== 2 · Building walk boundary ({CLIP_METHOD}, padding={PADDING_M} m) ==")

walk_mask  = nodes["layers"].str.contains("walk")
walk_nodes = nodes[walk_mask]
walk_xy    = walk_nodes[["x", "y"]].values

if CLIP_METHOD == "convex_hull":
    hull      = ConvexHull(walk_xy)
    hull_pts  = walk_xy[hull.vertices]   # (K, 2) vertices in order

    # Expand hull outward by PADDING_M metres.
    # For each vertex, move it away from the centroid by PADDING_M.
    centroid  = hull_pts.mean(axis=0)
    dirs      = hull_pts - centroid
    norms     = np.linalg.norm(dirs, axis=1, keepdims=True)
    hull_padded = hull_pts + (dirs / norms) * PADDING_M

    print(f"  Convex hull vertices : {len(hull_pts)}")
    print(f"  Hull area            : {hull.volume / 1e6:.1f} km²")  # volume=area in 2D

    # Point-in-hull test using the half-plane equations
    # hull.equations: each row is [a, b, c] where ax + by + c <= 0 means inside.
    # We re-derive equations from the padded hull for the membership test.
    from scipy.spatial import Delaunay
    hull_delaunay = Delaunay(hull_padded)

    def inside_boundary(xy_array: np.ndarray) -> np.ndarray:
        """Return boolean mask: True if point is inside padded convex hull."""
        return hull_delaunay.find_simplex(xy_array) >= 0

else:  # bbox
    x_min, x_max = walk_xy[:, 0].min() - PADDING_M, walk_xy[:, 0].max() + PADDING_M
    y_min, y_max = walk_xy[:, 1].min() - PADDING_M, walk_xy[:, 1].max() + PADDING_M
    hull_padded  = np.array([[x_min, y_min], [x_max, y_min],
                             [x_max, y_max], [x_min, y_max]])

    def inside_boundary(xy_array: np.ndarray) -> np.ndarray:
        return (
                (xy_array[:, 0] >= x_min) & (xy_array[:, 0] <= x_max) &
                (xy_array[:, 1] >= y_min) & (xy_array[:, 1] <= y_max)
        )

# ═══════════════════════════════════════════════════════════════════════════════
# § 3 · Classify and filter nodes
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 3 · Filtering nodes =================================================")

node_xy = nodes[["x", "y"]].values
inside  = inside_boundary(node_xy)

# Walk nodes are always kept (they define the boundary)
is_walk    = nodes["layers"].str.contains("walk").values
keep_mask  = is_walk | inside

nodes_kept    = nodes[keep_mask].copy()
nodes_removed = nodes[~keep_mask].copy()
removed_ids   = set(nodes_removed["id"])

removed_by_layer = {}
for _, row in nodes_removed.iterrows():
    for lyr in str(row["layers"]).split(","):
        removed_by_layer[lyr] = removed_by_layer.get(lyr, 0) + 1

print(f"  Nodes kept    : {len(nodes_kept):,}")
print(f"  Nodes removed : {len(nodes_removed):,}  {removed_by_layer}")

# ═══════════════════════════════════════════════════════════════════════════════
# § 4 · Filter edges
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 4 · Filtering edges =================================================")

edge_keep = ~(edges["source"].isin(removed_ids) | edges["target"].isin(removed_ids))
edges_kept    = edges[edge_keep].copy()
edges_removed = edges[~edge_keep].copy()

print(f"  Edges kept    : {len(edges_kept):,}")
print(f"  Edges removed : {len(edges_removed):,}")

removed_by_mode = edges_removed["mode"].value_counts().to_dict()
print(f"  Removed by mode: {removed_by_mode}")

# ═══════════════════════════════════════════════════════════════════════════════
# § 5 · Connectivity check
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 5 · Connectivity check ==============================================")

G = nx.MultiGraph()
for _, row in edges_kept.iterrows():
    G.add_edge(row["source"], row["target"], mode=row["mode"], weight=row["weight"])

components  = list(nx.connected_components(G))
largest     = max(len(c) for c in components)
isolated    = set(nodes_kept["id"]) - set(G.nodes())

print(f"  Nodes      : {len(nodes_kept):,}")
print(f"  Edges      : {len(edges_kept):,}")
print(f"  Components : {len(components)}")
print(f"  Largest    : {largest:,}")
print(f"  Isolated   : {len(isolated):,}")

# ═══════════════════════════════════════════════════════════════════════════════
# § 6 · Save
# ═══════════════════════════════════════════════════════════════════════════════
nodes_kept.to_csv(OUTPUT_NODES, index=False)
edges_kept.to_csv(OUTPUT_EDGES, index=False)
print(f"\n  Saved → {OUTPUT_NODES}")
print(f"  Saved → {OUTPUT_EDGES}")

# ═══════════════════════════════════════════════════════════════════════════════
# § 7 · Visual output
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 6 · Generating figure ===============================================")

from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D
from matplotlib.patches import Polygon as MplPolygon

# White theme
BG = "white"
C_WALK     = "#34D399"   # city street/walk layout
C_BUS      = "#2563EB"   # bus
C_METRO    = "#DC2626"   # metro
C_TRANSFER = "#F59E0B"   # transfer / hull
C_XCHANGE  = "#9333EA"   # bus+metro interchange

def _has(row, layer):
    return layer in str(row["layers"]).split(",")

def classify(df):
    df = df.copy()
    df["has_walk"]  = df.apply(lambda r: _has(r, "walk"),  axis=1)
    df["has_bus"]   = df.apply(lambda r: _has(r, "bus"),   axis=1)
    df["has_metro"] = df.apply(lambda r: _has(r, "metro"), axis=1)

    df["mask_walk"]  = df["has_walk"] & ~df["has_bus"] & ~df["has_metro"]
    df["mask_bus"]   = df["has_bus"] & ~df["has_metro"]
    df["mask_metro"] = df["has_metro"] & ~df["has_bus"]
    df["mask_xchg"]  = df["has_bus"] & df["has_metro"]

    return df

nodes_kept = classify(nodes_kept)

def build_segs(edge_df, node_df):
    xy = node_df.set_index("id")[["x", "y"]]

    def _get(nid):
        try:
            r = xy.loc[nid]
            return (float(r["x"]), float(r["y"]))
        except KeyError:
            return None

    def _grp(mode):
        return "transfer" if "transfer" in str(mode) else mode

    segs = {g: [] for g in ["walk", "bus", "metro", "transfer"]}

    for _, row in edge_df.iterrows():
        s = _get(row["source"])
        t = _get(row["target"])

        if s and t:
            group = _grp(row["mode"])
            if group in segs:
                segs[group].append([s, t])

    return segs

print("  Building edge segments ...")
segs_kept = build_segs(edges_kept, nodes_kept)

fig, ax = plt.subplots(figsize=(16, 9), facecolor=BG)
ax.set_facecolor(BG)

# Optional hull boundary
if CLIP_METHOD == "convex_hull":
    poly = MplPolygon(
        np.vstack([hull_padded, hull_padded[0]]),
        closed=True,
        fill=False,
        edgecolor=C_TRANSFER,
        linewidth=1.2,
        linestyle="--",
        alpha=0.65,
        zorder=5
    )
    ax.add_patch(poly)

# Draw edges — city layout first
for group, color, lw, alpha, zorder in [
    ("walk",     C_WALK,     0.35, 0.28, 1),
    ("transfer", C_TRANSFER, 0.8,  0.35, 2),
    ("bus",      C_BUS,      0.9,  0.55, 3),
    ("metro",    C_METRO,    1.8,  0.85, 4),
]:
    segs = segs_kept.get(group, [])
    if segs:
        ax.add_collection(
            LineCollection(
                segs,
                colors=color,
                linewidths=lw,
                alpha=alpha,
                zorder=zorder
            )
        )

# Draw important transit nodes only
for col, color, size, marker, label, zorder in [
    ("mask_bus",   C_BUS,    10, "o", "Bus stops", 5),
    ("mask_metro", C_METRO,  35, "o", "Metro stops", 6),
    ("mask_xchg",  C_XCHANGE, 80, "*", "Bus + Metro", 7),
]:
    sub = nodes_kept[nodes_kept[col]]
    if not sub.empty:
        ax.scatter(
            sub["x"],
            sub["y"],
            s=size,
            c=color,
            alpha=0.85,
            linewidths=0,
            marker=marker,
            label=label,
            zorder=zorder
        )

ax.autoscale_view()
ax.set_aspect("equal")

# Tighten the visible area around the graph
all_x = nodes_kept["x"].values
all_y = nodes_kept["y"].values

pad_x = (all_x.max() - all_x.min()) * 0.03
pad_y = (all_y.max() - all_y.min()) * 0.03

ax.set_xlim(all_x.min() - pad_x, all_x.max() + pad_x)
ax.set_ylim(all_y.min() - pad_y, all_y.max() + pad_y)

# Remove axes and grid
ax.axis("off")

fig.suptitle(
    "Porto Transit Graph Clipped to Walkable City Area",
    fontsize=18,
    fontweight="bold",
    color="#111111",
    y=0.98
)

description = (
    f"Remaining graph: {len(nodes_kept):,} nodes and "
    f"{len(edges_kept):,} edges after clipping transit nodes outside the walk area."
)

fig.text(
    0.5,
    0.94,
    description,
    ha="center",
    va="center",
    fontsize=11,
    color="#444444"
)

legend_items = [
    Line2D([0], [0], color=C_WALK, lw=1.4, alpha=0.8, label="Walk network"),
    Line2D([0], [0], color=C_BUS, lw=2, label="Bus"),
    Line2D([0], [0], color=C_METRO, lw=2.5, label="Metro"),
    Line2D([0], [0], marker="*", color="w", markerfacecolor=C_XCHANGE,
           markersize=12, label="Bus + Metro"),
    Line2D([0], [0], color=C_TRANSFER, lw=1.2, linestyle="--",
           label="Walk-area boundary"),
]

ax.legend(
    handles=legend_items,
    loc="lower left",
    frameon=True,
    facecolor="white",
    edgecolor="#DDDDDD",
    fontsize=10
)

# Use subplots_adjust instead of a huge default top margin
fig.subplots_adjust(left=0.01, right=0.99, bottom=0.01, top=0.90)

plt.savefig(
    "graph_clipped.png",
    dpi=180,
    bbox_inches="tight",
    pad_inches=0.08,
    facecolor=BG
)

plt.close(fig)