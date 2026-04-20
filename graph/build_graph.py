"""
Porto Public-Transport Multi-Modal Graph Builder
================================================
Layers
  walk   – OSMnx pedestrian street network (Porto, Portugal)
  bus    – STCP bus network   (GTFS · auto-downloaded from opendata.porto.digital)
  metro  – Metro do Porto     (GTFS · manual download · see NOTE, or OSMnx fallback)

NOTE – Metro GTFS (one-time manual step)
  Porto Digital blocks automated Metro downloads. Download manually from:
    https://opendata.porto.digital/en/dataset/horarios-paragens-e-rotas-em-formato-gtfs
  Save the file as metro_gtfs.zip next to this script. If absent, falls back
  to OSMnx rail geometry (less accurate).

Pipeline
  1  Download / cache data sources
  2  Extract walk layer from OSMnx
  3  Build bus + metro layers from GTFS (or OSMnx fallback)
  4  Build transfer edges between layers
  5  Connectivity check + pre-normalisation diagnostics  → graph_diagnostics.png
  6  Merge same-place nodes across layers (Union-Find)
  7  Convert edge weights: metres → seconds (time-based routing)
  8  Save nodes_norm.csv + edges_norm.csv
  9  Post-normalisation diagnostics                      → graph_normalised.png

Edge weight unit: SECONDS
  walk edge      = metres / SPEED_WALK
  bus edge       = metres / SPEED_BUS
  metro edge     = metres / SPEED_METRO
  transfer edge  = metres / SPEED_WALK + BOARDING_PENALTY_S
  Without this conversion Dijkstra always prefers walking (lower metre count)
  even when transit is much faster.

Outputs
  nodes.csv            raw nodes (pre-normalisation)
  edges.csv            raw edges (pre-normalisation, metres)
  nodes_norm.csv       canonical nodes (post-normalisation)
  edges_norm.csv       canonical edges (weight = seconds, weight_m = metres)
  graph_diagnostics.png
  graph_normalised.png
"""

# ═══════════════════════════════════════════════════════════════════════════════
# Imports
# ═══════════════════════════════════════════════════════════════════════════════
import os
import re
import zipfile
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D

import networkx as nx
import numpy as np
import osmnx as ox
import pandas as pd
import pyproj
import requests
from scipy.spatial import cKDTree

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════
PLACE = "Porto, Portugal"

GTFS_BUS_PAGE    = "https://opendata.porto.digital/dataset/horarios-paragens-e-rotas-em-formato-gtfs-stcp"
GTFS_BUS_LOCAL   = "stcp_gtfs.zip"
GTFS_METRO_LOCAL = "metro_gtfs.zip"

WALK_GRAPHML  = "porto_walk.graphml"
METRO_GRAPHML = "porto_metro_osmnx.graphml"

# Transfer radii (metres)
TRANSFER_BUS_WALK   = 100
TRANSFER_METRO_WALK = 150
TRANSFER_BUS_METRO  = 200

# Normalisation
MERGE_RADIUS = 15   # metres — nodes closer than this → same canonical node

# Speed constants for weight conversion (metres → seconds)
SPEED_WALK         = 1.4    # m/s  ≈  5.0 km/h
SPEED_BUS          = 6.9    # m/s  ≈ 25.0 km/h  (STCP urban avg incl. stops)
SPEED_METRO        = 11.1   # m/s  ≈ 40.0 km/h  (Metro do Porto commercial avg)
BOARDING_PENALTY_S = 180    # seconds flat wait added to every transfer edge

SPEED_MAP = {
    "walk"                 : SPEED_WALK,
    "bus"                  : SPEED_BUS,
    "metro"                : SPEED_METRO,
    "transfer_bus_walk"    : SPEED_WALK,
    "transfer_metro_walk"  : SPEED_WALK,
    "transfer_bus_metro"   : SPEED_WALK,
}
TRANSFER_MODES = {k for k in SPEED_MAP if k.startswith("transfer")}

# Plot theme
BG, PANEL, GRID_C = "#0D1117", "#161B22", "#21262D"
C_WALK, C_BUS, C_METRO = "#34D399", "#60A5FA", "#F87171"
C_TRANSFER, C_XCHANGE  = "#FBBF24", "#E879F9"
ACCENT = "#FBBF24"

# ═══════════════════════════════════════════════════════════════════════════════
# § 1 · Helper functions
# ═══════════════════════════════════════════════════════════════════════════════

def find_latest_gtfs_url(page_url: str) -> str:
    resp = requests.get(page_url, timeout=30)
    resp.raise_for_status()
    pattern = r'(https?://opendata\.porto\.digital/dataset/[^"\']+/download/[^"\']+\.zip)'
    urls = [u for u in re.findall(pattern, resp.text) if not u.endswith("/___")]
    if not urls:
        raise RuntimeError(f"No valid .zip links found on: {page_url}")
    return urls[-1]


def download_gtfs_auto(local_path: str, page_url: str) -> None:
    if os.path.exists(local_path):
        print(f"  [cache]    {local_path}")
        return
    print(f"  [discover] {page_url} ...")
    url = find_latest_gtfs_url(page_url)
    print(f"             → {url}")
    r = requests.get(url, timeout=120)
    r.raise_for_status()
    with open(local_path, "wb") as f:
        f.write(r.content)
    print(f"  [saved]    {local_path} ({len(r.content)/1024:.0f} KB)")


def read_gtfs_table(zip_path: str, filename: str) -> pd.DataFrame:
    with zipfile.ZipFile(zip_path) as zf:
        match = next((n for n in zf.namelist() if n.endswith(filename)), None)
        if match is None:
            raise FileNotFoundError(f"{filename} not found in {zip_path}")
        with zf.open(match) as f:
            return pd.read_csv(f, dtype=str, encoding="utf-8-sig")


def project_stops(stops_df: pd.DataFrame, transformer) -> pd.DataFrame:
    x, y = transformer.transform(
        stops_df["stop_lon"].astype(float).values,
        stops_df["stop_lat"].astype(float).values,
    )
    return stops_df.assign(utm_x=x, utm_y=y)


def style_ax(ax):
    ax.set_facecolor(PANEL)
    for sp in ax.spines.values():
        sp.set_color(GRID_C)


# ═══════════════════════════════════════════════════════════════════════════════
# § 2 · Download / cache data
# ═══════════════════════════════════════════════════════════════════════════════
print("== 1 · Downloading data ================================================")

download_gtfs_auto(GTFS_BUS_LOCAL, GTFS_BUS_PAGE)

metro_source = "gtfs"
if not os.path.exists(GTFS_METRO_LOCAL):
    print(f"\n  [WARNING] {GTFS_METRO_LOCAL} not found — falling back to OSMnx.")
    print("  Download manually: https://opendata.porto.digital/en/dataset/horarios-paragens-e-rotas-em-formato-gtfs")
    metro_source = "osmnx"
else:
    try:
        with zipfile.ZipFile(GTFS_METRO_LOCAL) as zf:
            zf.namelist()
        print(f"  [cache]    {GTFS_METRO_LOCAL}")
    except zipfile.BadZipFile:
        print(f"  [ERROR]    {GTFS_METRO_LOCAL} is corrupt — falling back to OSMnx.")
        os.remove(GTFS_METRO_LOCAL)
        metro_source = "osmnx"

if os.path.exists(WALK_GRAPHML):
    print(f"  [cache]    {WALK_GRAPHML}")
    g_walk_raw = ox.load_graphml(WALK_GRAPHML)
else:
    print(f"  [download] Walk graph for '{PLACE}' ...")
    g_walk_raw = ox.graph_from_place(PLACE, network_type="walk")
    ox.save_graphml(g_walk_raw, WALK_GRAPHML)

g_walk  = ox.project_graph(g_walk_raw)
UTM_CRS = g_walk.graph["crs"]
print(f"  Walk projected to: {UTM_CRS}")

g_metro_osmnx = None
if metro_source == "osmnx":
    if os.path.exists(METRO_GRAPHML):
        print(f"  [cache]    {METRO_GRAPHML}")
        g_metro_osmnx = ox.project_graph(ox.load_graphml(METRO_GRAPHML))
    else:
        print("  [download] Metro rail graph (OSMnx fallback) ...")
        raw = ox.graph_from_place(
            PLACE, network_type="all",
            custom_filter='["railway"~"subway|light_rail"]["railway"!~"platform"]',
        )
        ox.save_graphml(raw, METRO_GRAPHML)
        g_metro_osmnx = ox.project_graph(raw)

transformer = pyproj.Transformer.from_crs("EPSG:4326", UTM_CRS, always_xy=True)

# ═══════════════════════════════════════════════════════════════════════════════
# § 3 · Build layers
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 2 · Building layers =================================================")

final_nodes: list[dict] = []
final_edges: list[dict] = []

# Walk
for nid, d in g_walk.nodes(data=True):
    final_nodes.append({"id": f"walk_{nid}", "x": d["x"], "y": d["y"],
                        "name": "", "layer": "walk"})
for u, v, d in g_walk.edges(data=True):
    final_edges.append({"source": f"walk_{u}", "target": f"walk_{v}",
                        "weight": d.get("length", 1.0), "mode": "walk"})

print(f"  Walk  nodes: {sum(1 for n in final_nodes if n['layer']=='walk'):,}")
print(f"  Walk  edges: {sum(1 for e in final_edges if e['mode']=='walk'):,}")


def build_gtfs_layer(zip_path: str, mode: str) -> tuple[list[dict], list[dict]]:
    stops = project_stops(read_gtfs_table(zip_path, "stops.txt"), transformer)
    stop_times = (
        read_gtfs_table(zip_path, "stop_times.txt")[["trip_id", "stop_sequence", "stop_id"]]
        .assign(stop_sequence=lambda df: df["stop_sequence"].astype(int))
        .sort_values(["trip_id", "stop_sequence"])
    )
    nodes = [
        {"id": f"{mode}_{r['stop_id']}", "x": r["utm_x"], "y": r["utm_y"],
         "name": r.get("stop_name", ""), "layer": mode}
        for _, r in stops.iterrows()
    ]
    stop_xy = {r["stop_id"]: (r["utm_x"], r["utm_y"]) for _, r in stops.iterrows()}
    seen: set[tuple] = set()
    edges = []
    for _, group in stop_times.groupby("trip_id"):
        ids = group["stop_id"].tolist()
        for a, b in zip(ids, ids[1:]):
            if (a, b) in seen or a not in stop_xy or b not in stop_xy:
                continue
            seen.add((a, b))
            ax, ay = stop_xy[a]
            bx, by = stop_xy[b]
            edges.append({"source": f"{mode}_{a}", "target": f"{mode}_{b}",
                          "weight": max(float(np.hypot(bx-ax, by-ay)), 1.0), "mode": mode})
    return nodes, edges


def build_osmnx_metro_layer(G) -> tuple[list[dict], list[dict]]:
    nodes = [{"id": f"metro_{nid}", "x": d["x"], "y": d["y"],
              "name": d.get("name", ""), "layer": "metro"}
             for nid, d in G.nodes(data=True)]
    edges = [{"source": f"metro_{u}", "target": f"metro_{v}",
              "weight": d.get("length", 1.0), "mode": "metro"}
             for u, v, d in G.edges(data=True)]
    return nodes, edges


# Bus
bus_nodes, bus_edges = build_gtfs_layer(GTFS_BUS_LOCAL, "bus")
print(f"  Bus   nodes: {len(bus_nodes):,}  edges: {len(bus_edges):,}")

# Metro
if metro_source == "gtfs":
    metro_nodes, metro_edges = build_gtfs_layer(GTFS_METRO_LOCAL, "metro")
else:
    metro_nodes, metro_edges = build_osmnx_metro_layer(g_metro_osmnx)
print(f"  Metro nodes: {len(metro_nodes):,}  edges: {len(metro_edges):,}"
      + (" [OSMnx fallback]" if metro_source == "osmnx" else ""))

final_nodes.extend(bus_nodes + metro_nodes)
final_edges.extend(bus_edges + metro_edges)

# ═══════════════════════════════════════════════════════════════════════════════
# § 4 · Transfer edges
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 3 · Transfer edges ==================================================")

def make_transfers(src: list[dict], tgt: list[dict],
                   max_dist: float, label: str) -> int:
    if not tgt:
        return 0
    tree = cKDTree([(n["x"], n["y"]) for n in tgt])
    count = 0
    for s in src:
        dist, idx = tree.query((s["x"], s["y"]), distance_upper_bound=max_dist)
        if dist == np.inf:
            continue
        # Weight is raw metres here; converted to seconds in § 7
        final_edges.append({"source": s["id"],        "target": tgt[idx]["id"], "weight": dist, "mode": label})
        count += 1
    return count

walk_nodes = [n for n in final_nodes if n["layer"] == "walk"]
n_bw = make_transfers(bus_nodes,   walk_nodes,  TRANSFER_BUS_WALK,   "transfer_bus_walk")
n_mw = make_transfers(metro_nodes, walk_nodes,  TRANSFER_METRO_WALK, "transfer_metro_walk")
n_bm = make_transfers(bus_nodes,   metro_nodes, TRANSFER_BUS_METRO,  "transfer_bus_metro")
print(f"  Bus  <-> Walk  : {n_bw:,}")
print(f"  Metro <-> Walk : {n_mw:,}")
print(f"  Bus  <-> Metro : {n_bm:,}")

# ═══════════════════════════════════════════════════════════════════════════════
# § 5 · Save raw CSVs + pre-normalisation diagnostics
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 4 · Pre-normalisation diagnostics ===================================")

nodes_df = pd.DataFrame(final_nodes)
edges_df = pd.DataFrame(final_edges)

nodes_df.to_csv("nodes.csv", index=False)
edges_df.to_csv("edges.csv", index=False)
print("  Saved → nodes.csv, edges.csv")

G_check  = nx.Graph()
for e in final_edges:
    G_check.add_edge(e["source"], e["target"])

components  = list(nx.connected_components(G_check))
largest     = max(len(c) for c in components)
isolated    = {n["id"] for n in final_nodes} - set(G_check.nodes())
degree      = pd.concat([edges_df["source"], edges_df["target"]]).value_counts()
degree_df   = degree.reset_index()
degree_df.columns = ["node_id", "degree"]
degree_df["layer"] = degree_df["node_id"].str.split("_").str[0]

print(f"  Nodes: {len(final_nodes):,}  Edges: {len(final_edges):,}  "
      f"Components: {len(components)}  Largest: {largest:,}  Isolated: {len(isolated):,}")

# Diagnostics figure
fig = plt.figure(figsize=(20, 13), facecolor=BG)
gs  = gridspec.GridSpec(2, 3, figure=fig, hspace=0.45, wspace=0.32)

ax1 = fig.add_subplot(gs[0, :2]); style_ax(ax1)
cap = min(int(degree.max()), 25)
for layer, color in {"walk": C_WALK, "bus": C_BUS, "metro": C_METRO}.items():
    sub = degree_df[degree_df["layer"] == layer]["degree"].clip(upper=cap)
    if not sub.empty:
        ax1.hist(sub, bins=range(1, cap+2), alpha=0.75, label=layer.capitalize(),
                 color=color, edgecolor=BG, linewidth=0.4)
ax1.set_title("Node Degree Distribution by Layer", color="white", fontsize=13, pad=10)
ax1.set_xlabel("Degree", color="#9CA3AF"); ax1.set_ylabel("Count", color="#9CA3AF")
ax1.tick_params(colors="#9CA3AF")
ax1.legend(facecolor=PANEL, labelcolor="white")

ax2 = fig.add_subplot(gs[0, 2]); style_ax(ax2)
mode_counts: dict = {}
for k, v in edges_df["mode"].value_counts().items():
    lbl = "transfers" if "transfer" in k else k
    mode_counts[lbl] = mode_counts.get(lbl, 0) + v
bars = ax2.barh(list(mode_counts.keys()), list(mode_counts.values()),
                color=[{"walk": C_WALK, "bus": C_BUS, "metro": C_METRO}.get(k, ACCENT)
                       for k in mode_counts],
                edgecolor=BG, height=0.55)
ax2.set_title("Edge Count by Mode", color="white", fontsize=13, pad=10)
ax2.set_xlabel("Edges", color="#9CA3AF"); ax2.tick_params(colors="#9CA3AF")
for bar, val in zip(bars, mode_counts.values()):
    ax2.text(val + 5, bar.get_y() + bar.get_height()/2,
             f"{val:,}", va="center", color="#E5E7EB", fontsize=8)

ax3 = fig.add_subplot(gs[1, :]); style_ax(ax3)
for layer, color, size, alpha, zo in [
    ("walk",  C_WALK,  0.3, 0.18, 1),
    ("bus",   C_BUS,   8.0, 0.65, 2),
    ("metro", C_METRO, 25., 0.95, 3),
]:
    sub = nodes_df[nodes_df["layer"] == layer]
    lbl = f"{layer.capitalize()} ({len(sub):,})"
    if layer == "metro" and metro_source == "osmnx":
        lbl += " [OSMnx]"
    ax3.scatter(sub["x"], sub["y"], s=size, c=color, alpha=alpha,
                linewidths=0, label=lbl, zorder=zo)
ax3.set_title("Spatial Layout — Walk · Bus · Metro", color="white", fontsize=13, pad=10)
ax3.set_xlabel("Easting (m UTM)", color="#9CA3AF"); ax3.set_ylabel("Northing (m UTM)", color="#9CA3AF")
ax3.tick_params(colors="#9CA3AF")
ax3.legend(facecolor=PANEL, labelcolor="white", markerscale=4, loc="upper right")

fig.text(0.5, 0.01,
         f"Nodes: {len(final_nodes):,}  |  Edges: {len(final_edges):,}  |  "
         f"Components: {len(components)}  |  Largest: {largest:,}  |  "
         f"Isolated: {len(isolated):,}  |  Metro: {metro_source}",
         ha="center", color="#9CA3AF", fontsize=9)
fig.suptitle("Porto Multi-Modal Graph — Build Diagnostics",
             color="white", fontsize=16, y=0.99)
plt.savefig("graph_diagnostics.png", dpi=150, bbox_inches="tight", facecolor=BG)
plt.close(fig)
print("  Saved → graph_diagnostics.png")

# ═══════════════════════════════════════════════════════════════════════════════
# § 6 · Node normalisation (Union-Find spatial merge)
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 5 · Node normalisation ==============================================")
print(f"  Merge radius : {MERGE_RADIUS} m")

coords = nodes_df[["x", "y"]].values
ids    = nodes_df["id"].tolist()
names  = nodes_df["name"].fillna("").astype(str).tolist()
layers = nodes_df["layer"].tolist()
N      = len(nodes_df)

parent = list(range(N))

def _find(i: int) -> int:
    while parent[i] != i:
        parent[i] = parent[parent[i]]
        i = parent[i]
    return i

def _union(i: int, j: int) -> None:
    ri, rj = _find(i), _find(j)
    if ri != rj:
        parent[ri] = rj

pairs = cKDTree(coords).query_pairs(MERGE_RADIUS)
for i, j in pairs:
    _union(i, j)

clusters: dict[int, list[int]] = defaultdict(list)
for idx in range(N):
    clusters[_find(idx)].append(idx)

print(f"  Original nodes    : {N:,}")
print(f"  Canonical nodes   : {len(clusters):,}  (reduced by {N - len(clusters):,})")

remap: dict[str, str] = {}
canonical_rows: list[dict] = []

for root, members in clusters.items():
    cx = float(np.mean([coords[m, 0] for m in members]))
    cy = float(np.mean([coords[m, 1] for m in members]))
    cname   = next((names[m] for m in members if names[m]), "")
    clayers = sorted({layers[m] for m in members})
    cid     = f"loc_{round(cx):.0f}_{round(cy):.0f}"
    canonical_rows.append({"id": cid, "x": round(cx, 3), "y": round(cy, 3),
                           "name": cname, "layers": ",".join(clayers)})
    for m in members:
        remap[ids[m]] = cid

canonical_df = pd.DataFrame(canonical_rows)

edges_df["source"] = edges_df["source"].map(remap).fillna(edges_df["source"])
edges_df["target"] = edges_df["target"].map(remap).fillna(edges_df["target"])

n_loops  = (edges_df["source"] == edges_df["target"]).sum()
edges_df = edges_df[edges_df["source"] != edges_df["target"]].copy()
print(f"  Self-loops removed: {n_loops:,}")

# ═══════════════════════════════════════════════════════════════════════════════
# § 7 · Convert weights: metres → seconds
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 6 · Converting weights to seconds ===================================")
print(f"  walk={SPEED_WALK} m/s  bus={SPEED_BUS} m/s  "
      f"metro={SPEED_METRO} m/s  boarding={BOARDING_PENALTY_S}s")

def dist_to_seconds(row) -> float:
    mode  = str(row["mode"])
    dist  = float(row["weight"])
    speed = SPEED_MAP.get(mode, SPEED_WALK)
    t     = dist / speed
    if mode in TRANSFER_MODES:
        t += BOARDING_PENALTY_S
    return round(t, 2)

edges_df["weight_m"] = edges_df["weight"].round(2)
edges_df["weight"]   = edges_df.apply(dist_to_seconds, axis=1)

# Deduplicate (source, target, mode) keeping minimum travel time
edges_df = (
    edges_df
    .groupby(["source", "target", "mode"], sort=False)
    .agg(weight=("weight", "min"), weight_m=("weight_m", "min"))
    .reset_index()
)

multi = edges_df.groupby(["source", "target"])["mode"].nunique()
print(f"  Final edges: {len(edges_df):,}  "
      f"  Multi-mode pairs: {(multi >= 2).sum():,}")
print("  Median travel time per mode:")
for mode, grp in edges_df.groupby("mode"):
    print(f"    {mode:30s}  median={grp['weight'].median():.0f}s  "
          f"p95={grp['weight'].quantile(0.95):.0f}s")

# ═══════════════════════════════════════════════════════════════════════════════
# § 8 · Save normalised CSVs
# ═══════════════════════════════════════════════════════════════════════════════
canonical_df.to_csv("nodes_norm.csv", index=False)
edges_df[["source", "target", "mode", "weight", "weight_m"]].to_csv(
    "edges_norm.csv", index=False
)
print("\n  Saved → nodes_norm.csv")
print("  Saved → edges_norm.csv  (weight=seconds  weight_m=metres)")

# Connectivity check
G_norm = nx.MultiGraph()
for _, row in edges_df.iterrows():
    G_norm.add_edge(row["source"], row["target"],
                    mode=row["mode"], weight=row["weight"])
components_norm = list(nx.connected_components(G_norm))
largest_norm    = max(len(c) for c in components_norm)
isolated_norm   = set(canonical_df["id"]) - set(G_norm.nodes())
print(f"  Nodes: {len(canonical_df):,}  Edges: {G_norm.number_of_edges():,}  "
      f"Components: {len(components_norm)}  Largest: {largest_norm:,}  "
      f"Isolated: {len(isolated_norm):,}")

# ═══════════════════════════════════════════════════════════════════════════════
# § 9 · Post-normalisation diagnostic figure
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 7 · Post-normalisation figure =======================================")

# Node classification
def _has(row, layer):
    return layer in str(row["layers"]).split(",")

canonical_df["has_walk"]  = canonical_df.apply(lambda r: _has(r, "walk"),  axis=1)
canonical_df["has_bus"]   = canonical_df.apply(lambda r: _has(r, "bus"),   axis=1)
canonical_df["has_metro"] = canonical_df.apply(lambda r: _has(r, "metro"), axis=1)

mask_walk  = canonical_df["has_walk"]  & ~canonical_df["has_bus"] & ~canonical_df["has_metro"]
mask_bus   = canonical_df["has_bus"]   & ~canonical_df["has_metro"]
mask_metro = canonical_df["has_metro"] & ~canonical_df["has_bus"]
mask_xchg  = canonical_df["has_bus"]   &  canonical_df["has_metro"]

deg_series = pd.concat([edges_df["source"], edges_df["target"]]).value_counts()
canonical_df["degree"] = canonical_df["id"].map(deg_series).fillna(0).astype(int)

# Edge segments per mode group
def _group(mode: str) -> str:
    return "transfer" if "transfer" in mode else mode

edges_df["group"] = edges_df["mode"].apply(_group)
xy_lookup = canonical_df.set_index("id")[["x", "y"]]

def _get_xy(nid):
    try:
        r = xy_lookup.loc[nid]
        return (float(r["x"]), float(r["y"]))
    except KeyError:
        return None

print("  Building edge segments ...")
segs: dict[str, list] = {g: [] for g in ["walk", "bus", "metro", "transfer"]}
for _, row in edges_df.iterrows():
    s, t = _get_xy(row["source"]), _get_xy(row["target"])
    if s and t:
        segs[row["group"]].append([s, t])
print(f"  Segments: { {g: f'{len(v):,}' for g, v in segs.items()} }")

# Figure
fig = plt.figure(figsize=(22, 14), facecolor=BG)
gs  = gridspec.GridSpec(3, 2, figure=fig,
                        left=0.03, right=0.97, top=0.93, bottom=0.04,
                        hspace=0.42, wspace=0.28,
                        width_ratios=[1.8, 1], height_ratios=[1, 1, 1])
ax_map  = fig.add_subplot(gs[:, 0])
ax_deg  = fig.add_subplot(gs[0, 1])
ax_edge = fig.add_subplot(gs[1, 1])
ax_tbl  = fig.add_subplot(gs[2, 1])
for ax in (ax_map, ax_deg, ax_edge, ax_tbl):
    style_ax(ax)

# Panel A — spatial map
ax_map.set_title("Normalised Multi-Modal Graph — Porto",
                 color="white", fontsize=14, pad=10, fontweight="bold")
for group, color, lw, alpha in [
    ("walk",     C_WALK,     0.35, 0.06),
    ("transfer", C_TRANSFER, 0.9,  0.38),
    ("bus",      C_BUS,      1.0,  0.55),
    ("metro",    C_METRO,    2.0,  0.88),
]:
    if segs[group]:
        ax_map.add_collection(
            LineCollection(segs[group], colors=color, linewidths=lw, alpha=alpha, zorder=1)
        )
for mask, color, sz, zo, mk, lbl in [
    (mask_walk,  C_WALK,    0.3, 2, "o", f"Walk-only ({mask_walk.sum():,})"),
    (mask_bus,   C_BUS,     12,  3, "o", f"Bus stop ({mask_bus.sum():,})"),
    (mask_metro, C_METRO,   40,  4, "o", f"Metro stop ({mask_metro.sum():,})"),
    (mask_xchg,  C_XCHANGE, 90,  5, "*", f"Bus+Metro interchange ({mask_xchg.sum():,})"),
]:
    sub = canonical_df[mask]
    if not sub.empty:
        ax_map.scatter(sub["x"], sub["y"], s=sz, c=color, alpha=0.85,
                       linewidths=0, zorder=zo, marker=mk, label=lbl)
ax_map.autoscale_view()
ax_map.set_aspect("equal")
ax_map.set_xlabel("Easting (m, UTM)", color="#9CA3AF", fontsize=9)
ax_map.set_ylabel("Northing (m, UTM)", color="#9CA3AF", fontsize=9)
ax_map.tick_params(colors="#9CA3AF", labelsize=7)
extra_lines = [
    Line2D([0],[0], color=C_WALK,     lw=1.5, alpha=0.5, label="Walk edge"),
    Line2D([0],[0], color=C_BUS,      lw=1.5, alpha=0.8, label="Bus edge"),
    Line2D([0],[0], color=C_METRO,    lw=2.0, alpha=0.9, label="Metro edge"),
    Line2D([0],[0], color=C_TRANSFER, lw=1.5, alpha=0.7, label="Transfer edge"),
]
h, l = ax_map.get_legend_handles_labels()
ax_map.legend(h + extra_lines, l + [x.get_label() for x in extra_lines],
              facecolor="#0D1117", labelcolor="white", fontsize=8,
              framealpha=0.85, loc="upper right", markerscale=2.5)

# Panel B — degree distribution
ax_deg.set_title("Degree Distribution", color="white", fontsize=11, pad=8)
transit_mask = canonical_df["has_bus"] | canonical_df["has_metro"]
cap = min(int(canonical_df["degree"].quantile(0.995)), 30)
for subset, color, lbl in [
    (canonical_df[~transit_mask]["degree"], C_WALK, "Walk-only"),
    (canonical_df[ transit_mask]["degree"], C_BUS,  "Transit-touching"),
]:
    ax_deg.hist(subset.clip(upper=cap), bins=range(0, cap+2),
                alpha=0.72, color=color, label=lbl, edgecolor=BG, linewidth=0.3)
ax_deg.set_xlabel("Degree (capped)", color="#9CA3AF", fontsize=8)
ax_deg.set_ylabel("Node count",      color="#9CA3AF", fontsize=8)
ax_deg.tick_params(colors="#9CA3AF", labelsize=7)
ax_deg.legend(facecolor=PANEL, labelcolor="white", fontsize=8)
ax_deg.grid(axis="y", color=GRID_C, linewidth=0.4)

# Panel C — edge counts
ax_edge.set_title("Edge Count by Mode", color="white", fontsize=11, pad=8)
gcounts = edges_df["group"].value_counts().reindex(
    ["walk", "bus", "metro", "transfer"], fill_value=0)
bars = ax_edge.barh(gcounts.index.tolist(), gcounts.values,
                    color=[C_WALK, C_BUS, C_METRO, C_TRANSFER],
                    edgecolor=BG, height=0.55)
ax_edge.set_xlabel("Edge count", color="#9CA3AF", fontsize=8)
ax_edge.tick_params(colors="#9CA3AF", labelsize=8)
ax_edge.grid(axis="x", color=GRID_C, linewidth=0.4)
mx = gcounts.values.max()
for bar, val in zip(bars, gcounts.values):
    ax_edge.text(val + mx*0.01, bar.get_y() + bar.get_height()/2,
                 f"{val:,}", va="center", color="#E5E7EB", fontsize=8)

# Panel D — top interchange table
ax_tbl.set_title("Top Interchange Nodes (Bus + Metro)", color="white", fontsize=11, pad=8)
ax_tbl.axis("off")
top = (canonical_df[mask_xchg | mask_metro | mask_bus]
       .nlargest(10, "degree")[["name", "layers", "degree"]]
       .reset_index(drop=True))
if top.empty:
    ax_tbl.text(0.5, 0.5, "No interchange nodes found",
                ha="center", va="center", color="#9CA3AF", fontsize=9,
                transform=ax_tbl.transAxes)
else:
    cell_text = [
        [(str(r["name"])[:22] + "…" if len(str(r["name"])) > 23 else str(r["name"])) or "—",
         str(r["layers"]), str(r["degree"])]
        for _, r in top.iterrows()
    ]
    tbl = ax_tbl.table(cellText=cell_text, colLabels=["Name", "Layers", "Degree"],
                       loc="center", cellLoc="left")
    tbl.auto_set_font_size(False); tbl.set_fontsize(8); tbl.scale(1, 1.45)
    for j in range(3):
        tbl[(0, j)].set_facecolor("#1F2937")
        tbl[(0, j)].set_text_props(color=C_TRANSFER, fontweight="bold")
    for i in range(1, len(cell_text)+1):
        for j in range(3):
            tbl[(i, j)].set_facecolor(PANEL if i % 2 == 0 else "#1A2233")
            tbl[(i, j)].set_text_props(color="#E5E7EB")
            tbl[(i, j)].set_edgecolor(GRID_C)

fig.text(0.5, 0.005,
         f"Canonical nodes: {len(canonical_df):,}  |  Edges: {len(edges_df):,}  |  "
         f"Bus stops: {int(mask_bus.sum()):,}  |  Metro stops: {int(mask_metro.sum()):,}  |  "
         f"Interchanges: {int(mask_xchg.sum()):,}  |  "
         f"Weights: seconds  (walk={SPEED_WALK} m/s  bus={SPEED_BUS} m/s  "
         f"metro={SPEED_METRO} m/s  boarding={BOARDING_PENALTY_S}s)",
         ha="center", color="#6B7280", fontsize=8)
fig.suptitle("Porto Public-Transport Graph — Post-Normalisation Diagnostics",
             color="white", fontsize=15, fontweight="bold", y=0.975)
plt.savefig("graph_normalised.png", dpi=150, bbox_inches="tight", facecolor=BG)
plt.close(fig)
print("  Saved → graph_normalised.png")
print("\n  Pipeline complete.")