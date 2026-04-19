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

def _has(row, layer):
    return layer in str(row["layers"]).split(",")

def classify(df):
    df = df.copy()
    df["has_walk"]  = df.apply(lambda r: _has(r, "walk"),  axis=1)
    df["has_bus"]   = df.apply(lambda r: _has(r, "bus"),   axis=1)
    df["has_metro"] = df.apply(lambda r: _has(r, "metro"), axis=1)
    df["mask_walk"]  = df["has_walk"]  & ~df["has_bus"] & ~df["has_metro"]
    df["mask_bus"]   = df["has_bus"]   & ~df["has_metro"]
    df["mask_metro"] = df["has_metro"] & ~df["has_bus"]
    df["mask_xchg"]  = df["has_bus"]   &  df["has_metro"]
    return df

nodes_kept    = classify(nodes_kept)
nodes_removed = classify(nodes_removed)

def build_segs(edge_df, node_df):
    xy = node_df.set_index("id")[["x", "y"]]
    def _get(nid):
        try:
            r = xy.loc[nid]; return (float(r["x"]), float(r["y"]))
        except KeyError:
            return None
    def _grp(mode):
        return "transfer" if "transfer" in mode else mode
    segs = {g: [] for g in ["walk", "bus", "metro", "transfer"]}
    for _, row in edge_df.iterrows():
        s, t = _get(row["source"]), _get(row["target"])
        if s and t:
            segs[_grp(row["mode"])].append([s, t])
    return segs

print("  Building edge segments ...")
segs_kept    = build_segs(edges_kept,    nodes_kept)
segs_removed = build_segs(edges_removed, pd.concat([nodes_kept, nodes_removed]))

def style_ax(ax):
    ax.set_facecolor(PANEL)
    for sp in ax.spines.values():
        sp.set_color(GRID_C)

# ── Figure: 1 row × 2 cols (before / after) + summary panel below ────────────
fig = plt.figure(figsize=(24, 16), facecolor=BG)
gs  = gridspec.GridSpec(
    2, 2, figure=fig,
    left=0.03, right=0.97, top=0.93, bottom=0.04,
    hspace=0.32, wspace=0.15,
    height_ratios=[2.2, 1],
)
ax_before = fig.add_subplot(gs[0, 0])
ax_after  = fig.add_subplot(gs[0, 1])
ax_bar    = fig.add_subplot(gs[1, 0])
ax_tbl    = fig.add_subplot(gs[1, 1])
for ax in (ax_before, ax_after, ax_bar, ax_tbl):
    style_ax(ax)

def draw_map(ax, node_df, edge_segs, title, hull_pts=None, show_removed=False):
    ax.set_title(title, color="white", fontsize=13, pad=8, fontweight="bold")

    # Hull boundary
    if hull_pts is not None:
        poly = MplPolygon(
            np.vstack([hull_pts, hull_pts[0]]),
            closed=True, fill=False,
            edgecolor=C_TRANSFER, linewidth=1.2, linestyle="--", alpha=0.6, zorder=5
        )
        ax.add_patch(poly)

    # Edges
    for group, color, lw, alpha in [
        ("walk",     C_WALK,     0.35, 0.06),
        ("transfer", C_TRANSFER, 0.9,  0.35),
        ("bus",      C_BUS,      1.0,  0.55),
        ("metro",    C_METRO,    2.0,  0.88),
    ]:
        segs = edge_segs.get(group, [])
        if segs:
            ax.add_collection(LineCollection(segs, colors=color, linewidths=lw,
                                             alpha=alpha, zorder=1))

    # Nodes
    for col, color, sz, zo, mk, lbl in [
        ("mask_walk",  C_WALK,    0.3, 2, "o", "Walk"),
        ("mask_bus",   C_BUS,     14,  3, "o", "Bus stop"),
        ("mask_metro", C_METRO,   45,  4, "o", "Metro stop"),
        ("mask_xchg",  C_XCHANGE, 95,  5, "*", "Bus+Metro"),
    ]:
        sub = node_df[node_df[col]]
        if not sub.empty:
            ax.scatter(sub["x"], sub["y"], s=sz, c=color, alpha=0.85,
                       linewidths=0, zorder=zo, marker=mk,
                       label=f"{lbl} ({len(sub):,})")

    # Removed nodes (shown faintly on the "before" panel)
    if show_removed and not nodes_removed.empty:
        ax.scatter(nodes_removed["x"], nodes_removed["y"],
                   s=8, c=C_REMOVED, alpha=0.4, linewidths=0,
                   zorder=2, marker="x", label=f"Outside hull ({len(nodes_removed):,})")

    ax.autoscale_view(); ax.set_aspect("equal")
    ax.set_xlabel("Easting (m, UTM)", color="#9CA3AF", fontsize=8)
    ax.set_ylabel("Northing (m, UTM)", color="#9CA3AF", fontsize=8)
    ax.tick_params(colors="#9CA3AF", labelsize=7)
    ax.legend(facecolor="#0D1117", labelcolor="white", fontsize=7.5,
              framealpha=0.85, loc="upper right", markerscale=2)

# Reconstruct "before" node frame for the before panel
nodes_all = pd.concat([nodes_kept, nodes_removed]).pipe(classify)
segs_all  = build_segs(pd.concat([edges_kept, edges_removed]), nodes_all)

draw_map(ax_before, nodes_all, segs_all,
         "Before clipping", hull_pts=hull_padded, show_removed=True)
draw_map(ax_after,  nodes_kept, segs_kept,
         "After clipping", hull_pts=hull_padded, show_removed=False)

# Panel C — node count comparison bar chart
ax_bar.set_title("Node Count: Before vs After", color="white", fontsize=11, pad=8)
categories = ["Walk-only", "Bus stop", "Metro stop", "Bus+Metro"]
before_counts = [
    nodes_all["mask_walk"].sum(),
    nodes_all["mask_bus"].sum(),
    nodes_all["mask_metro"].sum(),
    nodes_all["mask_xchg"].sum(),
]
after_counts = [
    nodes_kept["mask_walk"].sum(),
    nodes_kept["mask_bus"].sum(),
    nodes_kept["mask_metro"].sum(),
    nodes_kept["mask_xchg"].sum(),
]
x   = np.arange(len(categories))
w   = 0.35
colors_cat = [C_WALK, C_BUS, C_METRO, C_XCHANGE]
bars_b = ax_bar.bar(x - w/2, before_counts, w, label="Before",
                    color=colors_cat, alpha=0.4, edgecolor=BG)
bars_a = ax_bar.bar(x + w/2, after_counts,  w, label="After",
                    color=colors_cat, alpha=0.9, edgecolor=BG)
ax_bar.set_xticks(x); ax_bar.set_xticklabels(categories, color="#9CA3AF", fontsize=8)
ax_bar.set_ylabel("Count", color="#9CA3AF", fontsize=8)
ax_bar.tick_params(colors="#9CA3AF", labelsize=8)
ax_bar.grid(axis="y", color=GRID_C, linewidth=0.4)
ax_bar.legend(facecolor=PANEL, labelcolor="white", fontsize=8)
for bar in list(bars_b) + list(bars_a):
    v = bar.get_height()
    ax_bar.text(bar.get_x() + bar.get_width()/2, v + max(before_counts)*0.01,
                f"{int(v):,}", ha="center", color="#E5E7EB", fontsize=7)

# Panel D — summary table
ax_tbl.set_title("Clipping Summary", color="white", fontsize=11, pad=8)
ax_tbl.axis("off")

def pct(a, b):
    return f"{100*(b-a)/a:+.1f}%" if a else "—"

rows = [
    ["Walk nodes",   f"{nodes_all['mask_walk'].sum():,}",  f"{nodes_kept['mask_walk'].sum():,}",  pct(nodes_all['mask_walk'].sum(),  nodes_kept['mask_walk'].sum())],
    ["Bus stops",    f"{nodes_all['mask_bus'].sum():,}",   f"{nodes_kept['mask_bus'].sum():,}",   pct(nodes_all['mask_bus'].sum(),   nodes_kept['mask_bus'].sum())],
    ["Metro stops",  f"{nodes_all['mask_metro'].sum():,}", f"{nodes_kept['mask_metro'].sum():,}", pct(nodes_all['mask_metro'].sum(), nodes_kept['mask_metro'].sum())],
    ["Interchanges", f"{nodes_all['mask_xchg'].sum():,}",  f"{nodes_kept['mask_xchg'].sum():,}",  pct(nodes_all['mask_xchg'].sum(),  nodes_kept['mask_xchg'].sum())],
    ["Total nodes",  f"{len(nodes_all):,}",                f"{len(nodes_kept):,}",                pct(len(nodes_all), len(nodes_kept))],
    ["Total edges",  f"{len(edges):,}",                    f"{len(edges_kept):,}",                pct(len(edges), len(edges_kept))],
    ["Components",   "—",                                  f"{len(components)}",                  "—"],
    ["Largest comp", "—",                                  f"{largest:,} nodes",                  "—"],
]
tbl = ax_tbl.table(cellText=rows,
                   colLabels=["Metric", "Before", "After", "Change"],
                   loc="center", cellLoc="center")
tbl.auto_set_font_size(False); tbl.set_fontsize(9); tbl.scale(1, 1.6)
for j in range(4):
    tbl[(0, j)].set_facecolor("#1F2937")
    tbl[(0, j)].set_text_props(color=C_TRANSFER, fontweight="bold")
for i in range(1, len(rows)+1):
    for j in range(4):
        tbl[(i, j)].set_facecolor(PANEL if i % 2 == 0 else "#1A2233")
        tbl[(i, j)].set_text_props(color="#E5E7EB")
        tbl[(i, j)].set_edgecolor(GRID_C)

fig.text(0.5, 0.005,
         f"Clip method: {CLIP_METHOD}  |  Padding: {PADDING_M} m  |  "
         f"Removed: {len(nodes_removed):,} nodes, {len(edges_removed):,} edges",
         ha="center", color="#6B7280", fontsize=8.5)
fig.suptitle("Porto Graph — Transit Clipped to Walk Area",
             color="white", fontsize=15, fontweight="bold", y=0.975)

plt.savefig("graph_clipped.png", dpi=150, bbox_inches="tight", facecolor=BG)
plt.close(fig)
print("  Saved → graph_clipped.png")
print("\n  Done.")