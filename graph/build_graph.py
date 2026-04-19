"""
Porto Public-Transport Multi-Modal Graph Builder
=================================================
Layers
  walk   – OSMnx pedestrian street network (Porto, Portugal)
  bus    – STCP bus network   (GTFS · auto-downloaded from opendata.porto.digital)
  metro  – Metro do Porto     (GTFS · see NOTE below, or OSMnx fallback)

NOTE – Metro GTFS manual download (one-time)
  Porto Digital blocks automated downloads for the Metro feed.
  Download it manually from your browser and save as metro_gtfs.zip
  in the same folder as this script:
    https://opendata.porto.digital/en/dataset/horarios-paragens-e-rotas-em-formato-gtfs
  Click the most recent "GTFS Metro do Porto XX-XX-XXXX" entry → Download.
  Once metro_gtfs.zip exists, the script never needs to download it again.
  If the file is absent, the script falls back to OSMnx rail geometry.

Transfers (bidirectional + boarding penalty)
  bus stop   <-> nearest walk node   (≤ TRANSFER_BUS_WALK  metres)
  metro stop <-> nearest walk node   (≤ TRANSFER_METRO_WALK metres)
  bus stop   <-> nearest metro stop  (≤ TRANSFER_BUS_METRO  metres)

Output: nodes.csv, edges.csv, graph_diagnostics.png
"""

import os, re, zipfile, requests
import osmnx as ox
import pandas as pd
import numpy as np
from scipy.spatial import cKDTree
import pyproj
import networkx as nx
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

# ═══════════════════════════════════════════════════════════════════════════════
# 0 · Configuration
# ═══════════════════════════════════════════════════════════════════════════════
PLACE = "Porto, Portugal"

GTFS_BUS_PAGE    = "https://opendata.porto.digital/dataset/horarios-paragens-e-rotas-em-formato-gtfs-stcp"
GTFS_BUS_LOCAL   = "stcp_gtfs.zip"
GTFS_METRO_LOCAL = "metro_gtfs.zip"   # place here manually – see NOTE above

WALK_GRAPHML         = "porto_walk.graphml"
METRO_GRAPHML        = "porto_metro_osmnx.graphml"   # used only as fallback

TRANSFER_BUS_WALK    = 100   # metres
TRANSFER_METRO_WALK  = 150   # metres
TRANSFER_BUS_METRO   = 200   # metres
BOARDING_PENALTY     = 120   # metres equivalent per mode change

# ═══════════════════════════════════════════════════════════════════════════════
# 1 · Helpers
# ═══════════════════════════════════════════════════════════════════════════════
def find_latest_gtfs_url(page_url: str) -> str:
    """Scrape a CKAN dataset page and return the most recent working .zip URL."""
    resp = requests.get(page_url, timeout=30)
    resp.raise_for_status()
    pattern = r'(https?://opendata\.porto\.digital/dataset/[^"\']+/download/[^"\']+\.zip)'
    urls = [u for u in re.findall(pattern, resp.text) if not u.endswith("/___")]
    if not urls:
        raise RuntimeError(f"No valid .zip links found on: {page_url}")
    return urls[-1]  # last = most recent


def download_gtfs_auto(local_path: str, page_url: str) -> None:
    if os.path.exists(local_path):
        print(f"  [cache]    {local_path}")
        return
    print(f"  [discover] Finding latest URL from {page_url} ...")
    url = find_latest_gtfs_url(page_url)
    print(f"             → {url}")
    print(f"  [download] {local_path} ...")
    r = requests.get(url, timeout=120)
    r.raise_for_status()
    with open(local_path, "wb") as f:
        f.write(r.content)
    print(f"  [saved]    {local_path} ({len(r.content)/1024:.0f} KB)")


def read_gtfs_table(zip_path: str, filename: str) -> pd.DataFrame:
    with zipfile.ZipFile(zip_path) as zf:
        names = zf.namelist()
        match = next((n for n in names if n.endswith(filename)), None)
        if match is None:
            raise FileNotFoundError(f"{filename} not found in {zip_path}. Contents: {names}")
        with zf.open(match) as f:
            return pd.read_csv(f, dtype=str, encoding="utf-8-sig")


# ═══════════════════════════════════════════════════════════════════════════════
# 2 · Download / cache data sources
# ═══════════════════════════════════════════════════════════════════════════════
print("== 1 · Downloading data ================================================")

# Bus: auto-downloadable
download_gtfs_auto(GTFS_BUS_LOCAL, GTFS_BUS_PAGE)

# Metro: check presence, explain clearly if missing
metro_source = "gtfs"
if not os.path.exists(GTFS_METRO_LOCAL):
    print(f"\n  [WARNING] {GTFS_METRO_LOCAL} not found.")
    print("  Porto Digital blocks automated Metro downloads.")
    print("  → Download manually from your browser:")
    print("    https://opendata.porto.digital/en/dataset/horarios-paragens-e-rotas-em-formato-gtfs")
    print("  → Save the file as:  metro_gtfs.zip  (next to this script)")
    print("  → Re-run the script.")
    print("\n  Falling back to OSMnx rail geometry for metro layer.")
    print("  (Less accurate – no station tags, may include tunnel nodes.)\n")
    metro_source = "osmnx"
else:
    # Validate it's actually a zip (not a 0-byte or HTML error page)
    try:
        with zipfile.ZipFile(GTFS_METRO_LOCAL) as zf:
            _ = zf.namelist()
        print(f"  [cache]    {GTFS_METRO_LOCAL}")
    except zipfile.BadZipFile:
        sz = os.path.getsize(GTFS_METRO_LOCAL)
        print(f"\n  [ERROR] {GTFS_METRO_LOCAL} is not a valid zip ({sz} bytes).")
        print("  Delete it and download again manually:")
        print("  https://opendata.porto.digital/en/dataset/horarios-paragens-e-rotas-em-formato-gtfs")
        print("  Falling back to OSMnx rail geometry.\n")
        os.remove(GTFS_METRO_LOCAL)
        metro_source = "osmnx"

# Walk graph
if os.path.exists(WALK_GRAPHML):
    print(f"  [cache]    {WALK_GRAPHML}")
    g_walk_raw = ox.load_graphml(WALK_GRAPHML)
else:
    print(f"  [download] Walk graph for '{PLACE}' ...")
    g_walk_raw = ox.graph_from_place(PLACE, network_type="walk")
    ox.save_graphml(g_walk_raw, WALK_GRAPHML)
    print(f"  [saved]    {WALK_GRAPHML}")

g_walk  = ox.project_graph(g_walk_raw)
UTM_CRS = g_walk.graph["crs"]
print(f"  Walk projected to: {UTM_CRS}")

# OSMnx metro fallback (only if needed)
g_metro_osmnx = None
if metro_source == "osmnx":
    if os.path.exists(METRO_GRAPHML):
        print(f"  [cache]    {METRO_GRAPHML}")
        g_metro_osmnx = ox.project_graph(ox.load_graphml(METRO_GRAPHML))
    else:
        print(f"  [download] Metro rail graph (OSMnx fallback) ...")
        g_metro_osmnx = ox.project_graph(ox.graph_from_place(
            PLACE, network_type="all",
            custom_filter='["railway"~"subway|light_rail"]["railway"!~"platform"]'
        ))
        ox.save_graphml(ox.graph_from_place(
            PLACE, network_type="all",
            custom_filter='["railway"~"subway|light_rail"]["railway"!~"platform"]'
        ), METRO_GRAPHML)
        print(f"  [saved]    {METRO_GRAPHML}")

# ═══════════════════════════════════════════════════════════════════════════════
# 3 · WGS84 → UTM projector
# ═══════════════════════════════════════════════════════════════════════════════
transformer = pyproj.Transformer.from_crs("EPSG:4326", UTM_CRS, always_xy=True)

def project_stops(stops_df: pd.DataFrame) -> pd.DataFrame:
    lons = stops_df["stop_lon"].astype(float).values
    lats = stops_df["stop_lat"].astype(float).values
    x, y = transformer.transform(lons, lats)
    return stops_df.assign(utm_x=x, utm_y=y)

# ═══════════════════════════════════════════════════════════════════════════════
# 4 · Walk layer
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 2 · Extracting walk layer ===========================================")

final_nodes: list[dict] = []
final_edges: list[dict] = []

for nid, d in g_walk.nodes(data=True):
    final_nodes.append({"id": f"walk_{nid}", "x": d["x"], "y": d["y"],
                        "name": "", "layer": "walk"})
for u, v, d in g_walk.edges(data=True):
    final_edges.append({"source": f"walk_{u}", "target": f"walk_{v}",
                        "weight": d.get("length", 1.0), "mode": "walk"})

print(f"  Walk nodes : {sum(1 for n in final_nodes if n['layer']=='walk'):,}")
print(f"  Walk edges : {sum(1 for e in final_edges if e['mode']=='walk'):,}")

# ═══════════════════════════════════════════════════════════════════════════════
# 5 · GTFS layer builder
# ═══════════════════════════════════════════════════════════════════════════════
def build_gtfs_layer(zip_path: str, mode: str) -> tuple[list[dict], list[dict]]:
    print(f"\n  Processing {mode} GTFS ...")
    stops = project_stops(read_gtfs_table(zip_path, "stops.txt"))
    stop_times = (
        read_gtfs_table(zip_path, "stop_times.txt")
        [["trip_id", "stop_sequence", "stop_id"]]
        .assign(stop_sequence=lambda df: df["stop_sequence"].astype(int))
        .sort_values(["trip_id", "stop_sequence"])
    )
    layer_nodes = [
        {"id": f"{mode}_{r['stop_id']}", "x": r["utm_x"], "y": r["utm_y"],
         "name": r.get("stop_name", ""), "layer": mode}
        for _, r in stops.iterrows()
    ]
    stop_xy = {r["stop_id"]: (r["utm_x"], r["utm_y"]) for _, r in stops.iterrows()}
    seen: set[tuple] = set()
    layer_edges: list[dict] = []
    for _, group in stop_times.groupby("trip_id"):
        ids = group["stop_id"].tolist()
        for a, b in zip(ids, ids[1:]):
            if (a, b) in seen or a not in stop_xy or b not in stop_xy:
                continue
            seen.add((a, b))
            ax, ay = stop_xy[a]
            bx, by = stop_xy[b]
            layer_edges.append({
                "source": f"{mode}_{a}", "target": f"{mode}_{b}",
                "weight": max(float(np.hypot(bx-ax, by-ay)), 1.0), "mode": mode
            })
    print(f"    Stops (nodes) : {len(layer_nodes):,}")
    print(f"    Unique pairs  : {len(layer_edges):,}")
    return layer_nodes, layer_edges

# ═══════════════════════════════════════════════════════════════════════════════
# 6 · OSMnx metro fallback layer builder
# ═══════════════════════════════════════════════════════════════════════════════
def build_osmnx_metro_layer(G) -> tuple[list[dict], list[dict]]:
    print("\n  Processing metro (OSMnx fallback) ...")
    layer_nodes, layer_edges = [], []
    for nid, d in G.nodes(data=True):
        layer_nodes.append({"id": f"metro_{nid}", "x": d["x"], "y": d["y"],
                            "name": d.get("name",""), "layer": "metro"})
    for u, v, d in G.edges(data=True):
        layer_edges.append({"source": f"metro_{u}", "target": f"metro_{v}",
                            "weight": d.get("length", 1.0), "mode": "metro"})
    print(f"    Nodes : {len(layer_nodes):,}")
    print(f"    Edges : {len(layer_edges):,}")
    return layer_nodes, layer_edges

# ═══════════════════════════════════════════════════════════════════════════════
# 7 · Build all layers
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 3 · Building transport layers =======================================")

bus_nodes, bus_edges = build_gtfs_layer(GTFS_BUS_LOCAL, "bus")

if metro_source == "gtfs":
    metro_nodes, metro_edges = build_gtfs_layer(GTFS_METRO_LOCAL, "metro")
else:
    metro_nodes, metro_edges = build_osmnx_metro_layer(g_metro_osmnx)

final_nodes.extend(bus_nodes)
final_nodes.extend(metro_nodes)
final_edges.extend(bus_edges)
final_edges.extend(metro_edges)

# ═══════════════════════════════════════════════════════════════════════════════
# 8 · Transfer edges
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 4 · Building transfer edges =========================================")

def make_transfers(src: list[dict], tgt: list[dict], max_dist: float,
                   label: str, penalty: float = BOARDING_PENALTY) -> int:
    if not tgt:
        return 0
    tree = cKDTree([(n["x"], n["y"]) for n in tgt])
    count = 0
    for s in src:
        dist, idx = tree.query((s["x"], s["y"]), distance_upper_bound=max_dist)
        if dist == np.inf:
            continue
        w = dist + penalty
        final_edges.append({"source": s["id"], "target": tgt[idx]["id"],
                            "weight": w, "mode": label})
        final_edges.append({"source": tgt[idx]["id"], "target": s["id"],
                            "weight": w, "mode": label + "_r"})
        count += 1
    return count

walk_nodes = [n for n in final_nodes if n["layer"] == "walk"]
n_bw = make_transfers(bus_nodes,   walk_nodes,  TRANSFER_BUS_WALK,   "transfer_bus_walk")
n_mw = make_transfers(metro_nodes, walk_nodes,  TRANSFER_METRO_WALK, "transfer_metro_walk")
n_bm = make_transfers(bus_nodes,   metro_nodes, TRANSFER_BUS_METRO,  "transfer_bus_metro")

print(f"  Bus   <-> Walk  : {n_bw:,}")
print(f"  Metro <-> Walk  : {n_mw:,}")
print(f"  Bus   <-> Metro : {n_bm:,}")

# ═══════════════════════════════════════════════════════════════════════════════
# 9 · Connectivity & statistics
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 5 · Connectivity check ==============================================")

G_check = nx.Graph()
for e in final_edges:
    G_check.add_edge(e["source"], e["target"])

isolated_ids = {n["id"] for n in final_nodes} - set(G_check.nodes())
components   = list(nx.connected_components(G_check))
largest      = max(len(c) for c in components)
isolated_by_layer: dict = {}
for nid in isolated_ids:
    lyr = nid.split("_")[0]
    isolated_by_layer[lyr] = isolated_by_layer.get(lyr, 0) + 1

edges_df  = pd.DataFrame(final_edges)
nodes_df  = pd.DataFrame(final_nodes)
degree    = pd.concat([edges_df["source"], edges_df["target"]]).value_counts()
degree_df = degree.reset_index()
degree_df.columns = ["node_id", "degree"]
degree_df["layer"] = degree_df["node_id"].str.split("_").str[0]

print(f"  Total nodes              : {len(final_nodes):,}")
print(f"  Total edges              : {len(final_edges):,}")
print(f"  Connected components     : {len(components)}")
print(f"  Largest component        : {largest:,} nodes")
print(f"  Isolated nodes (0 edges) : {len(isolated_ids):,}  {isolated_by_layer}")
print(f"  Nodes with >= 2 edges    : {(degree >= 2).sum():,}")
print(f"  Nodes with 1 edge        : {(degree == 1).sum():,}  (dead-ends)")
print(f"  Max degree               : {degree.max()}")
print(f"  Avg degree               : {degree.mean():.2f}")
print()
for layer in ["walk", "bus", "metro"]:
    sub = degree_df[degree_df["layer"] == layer]
    if sub.empty: continue
    suffix = " [OSMnx fallback]" if layer == "metro" and metro_source == "osmnx" else ""
    print(f"  [{layer:5s}]{suffix}  nodes={len(sub):,}  "
          f"avg_deg={sub['degree'].mean():.2f}  max_deg={sub['degree'].max()}")

# ═══════════════════════════════════════════════════════════════════════════════
# 10 · Diagnostic plots
# ═══════════════════════════════════════════════════════════════════════════════
print("\n== 6 · Generating diagnostics ==========================================")

BG, PANEL  = "#0D1117", "#161B22"
COLORS = {"walk": "#34D399", "bus": "#60A5FA", "metro": "#F87171"}
ACCENT = "#FBBF24"

fig = plt.figure(figsize=(20, 13), facecolor=BG)
gs  = gridspec.GridSpec(2, 3, figure=fig, hspace=0.45, wspace=0.32)

# Degree distribution
ax1 = fig.add_subplot(gs[0, :2])
ax1.set_facecolor(PANEL)
cap = min(int(degree.max()), 25)
for layer, color in COLORS.items():
    sub = degree_df[degree_df["layer"] == layer]["degree"].clip(upper=cap)
    if sub.empty: continue
    ax1.hist(sub, bins=range(1, cap+2), alpha=0.75, label=layer.capitalize(),
             color=color, edgecolor=BG, linewidth=0.4)
ax1.set_title("Node Degree Distribution by Layer", color="white", fontsize=13, pad=10)
ax1.set_xlabel("Degree", color="#9CA3AF"); ax1.set_ylabel("Node count", color="#9CA3AF")
ax1.tick_params(colors="#9CA3AF")
ax1.legend(facecolor=PANEL, labelcolor="white", framealpha=0.9)
for sp in ax1.spines.values(): sp.set_color("#30363D")

# Edge count by mode
ax2 = fig.add_subplot(gs[0, 2])
ax2.set_facecolor(PANEL)
clean: dict = {}
for k, v in edges_df["mode"].value_counts().items():
    lbl = "transfers" if "transfer" in k else k
    clean[lbl] = clean.get(lbl, 0) + v
bars = ax2.barh(list(clean.keys()), list(clean.values()),
                color=[COLORS.get(k, ACCENT) for k in clean], edgecolor=BG, height=0.55)
ax2.set_title("Edge Count by Mode", color="white", fontsize=13, pad=10)
ax2.set_xlabel("Edges", color="#9CA3AF"); ax2.tick_params(colors="#9CA3AF")
for sp in ax2.spines.values(): sp.set_color("#30363D")
for bar, val in zip(bars, clean.values()):
    ax2.text(val+5, bar.get_y()+bar.get_height()/2, f"{val:,}",
             va="center", color="#E5E7EB", fontsize=8)

# Spatial scatter
ax3 = fig.add_subplot(gs[1, :])
ax3.set_facecolor(PANEL)
for layer, color, size, alpha, zo in [
    ("walk",  "#34D399", 0.3, 0.18, 1),
    ("bus",   "#60A5FA", 8.0, 0.65, 2),
    ("metro", "#F87171", 25., 0.95, 3),
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
ax3.legend(facecolor=PANEL, labelcolor="white", framealpha=0.9, markerscale=4, loc="upper right")
for sp in ax3.spines.values(): sp.set_color("#30363D")

metro_label = "GTFS" if metro_source == "gtfs" else "OSMnx fallback"
fig.text(0.5, 0.01,
         f"Nodes: {len(final_nodes):,}  |  Edges: {len(final_edges):,}  |  "
         f"Components: {len(components)}  |  Largest: {largest:,}  |  "
         f"Isolated: {len(isolated_ids):,}  |  Metro source: {metro_label}",
         ha="center", color="#9CA3AF", fontsize=9)
fig.suptitle("Porto Public-Transport Multi-Modal Graph — Build Diagnostics",
             color="white", fontsize=16, y=0.99)

plt.savefig("graph_diagnostics.png", dpi=150, bbox_inches="tight", facecolor=BG)
print("  Saved -> graph_diagnostics.png")

# ═══════════════════════════════════════════════════════════════════════════════
# 11 · Save CSVs
# ═══════════════════════════════════════════════════════════════════════════════
nodes_df.to_csv("nodes.csv", index=False)
edges_df.to_csv("edges.csv", index=False)
print("\n  nodes.csv and edges.csv saved.")
print("  Pipeline complete.")

# ═══════════════════════════════════════════════════════════════════════════════
# 12 · Node Normalisation  (merge same-place nodes across layers)
# ═══════════════════════════════════════════════════════════════════════════════
"""
Goal
----
Nodes from different layers (walk / bus / metro) that represent the SAME
physical location are collapsed into a single canonical node.  All edges
that referenced any of the original node IDs are remapped to the canonical
ID, so the final graph can have multiple edges with the same (source, target)
pair but different `mode` values – exactly as requested.

Algorithm
---------
1.  Build a KD-tree over ALL nodes.
2.  For each node, find every neighbour within MERGE_RADIUS metres.
3.  Union-Find (Disjoint Set Union) groups all mutually-close nodes into one
    cluster, regardless of layer.
4.  Each cluster gets one canonical node:
      • canonical_id  = "loc_<rounded_x>_<rounded_y>"   (grid-snapped, readable)
      • x, y          = centroid of the cluster
      • name          = first non-empty name found in the cluster
      • layers        = sorted list of all layers present  (e.g. "bus,walk")
5.  Edges: source / target remapped to canonical IDs.
    Duplicate (source, target, mode) triplets are deduplicated, keeping the
    minimum weight (shortest real distance wins).
6.  Self-loops created by the merge (source == target after remap) are dropped
    because they carry no routing information.

Tune
----
MERGE_RADIUS  – metres within which two nodes are considered the same place.
                Default 15 m.  Lower → fewer merges (safer for dense stops).
                Raise to ~30 m if your walk graph snaps to road centrelines far
                from the kerb where stops are tagged.
"""

import numpy as np
import pandas as pd
from scipy.spatial import cKDTree

# ── tuneable ──────────────────────────────────────────────────────────────────
MERGE_RADIUS = 15   # metres  ← adjust if needed
# ─────────────────────────────────────────────────────────────────────────────

print("\n== 7 · Node Normalisation ==============================================")
print(f"  Merge radius : {MERGE_RADIUS} m")

# ── reload from CSVs so this block is self-contained ─────────────────────────
# (if you paste this right after the main script, nodes_df / edges_df are
#  already in memory – the read_csv calls are just a safety net)
try:
    nodes_df  # noqa: F821  – already in memory
    edges_df  # noqa: F821
except NameError:
    nodes_df = pd.read_csv("nodes.csv",  dtype={"id": str, "name": str})
    edges_df = pd.read_csv("edges.csv",  dtype={"source": str, "target": str,
                                                "mode": str})

nodes_df = nodes_df.copy()
edges_df = edges_df.copy()
nodes_df["name"] = nodes_df["name"].fillna("").astype(str)

coords = nodes_df[["x", "y"]].values          # shape (N, 2)
ids    = nodes_df["id"].tolist()
names  = nodes_df["name"].tolist()
layers = nodes_df["layer"].tolist()
N      = len(nodes_df)

# ── Union-Find ────────────────────────────────────────────────────────────────
parent = list(range(N))

def find(i: int) -> int:
    while parent[i] != i:
        parent[i] = parent[parent[i]]   # path compression
        i = parent[i]
    return i

def union(i: int, j: int) -> None:
    ri, rj = find(i), find(j)
    if ri != rj:
        parent[ri] = rj

# ── spatial grouping ──────────────────────────────────────────────────────────
tree    = cKDTree(coords)
pairs   = tree.query_pairs(MERGE_RADIUS)       # all pairs within radius
for i, j in pairs:
    union(i, j)

# ── build clusters ────────────────────────────────────────────────────────────
from collections import defaultdict

clusters: dict[int, list[int]] = defaultdict(list)
for idx in range(N):
    clusters[find(idx)].append(idx)

print(f"  Original nodes           : {N:,}")
print(f"  Pairs within {MERGE_RADIUS} m          : {len(pairs):,}")
print(f"  Clusters (canonical nodes): {len(clusters):,}")
print(f"  Net node reduction       : {N - len(clusters):,}")

# ── build canonical nodes + remapping dict ────────────────────────────────────
GRID = 1   # round coords to nearest metre for readable IDs

remap: dict[str, str] = {}          # old node-id → canonical node-id
canonical_rows: list[dict] = []

for root, members in clusters.items():
    cx = float(np.mean([coords[m, 0] for m in members]))
    cy = float(np.mean([coords[m, 1] for m in members]))

    # Pick first non-empty name
    cname = next((names[m] for m in members if names[m]), "")

    # Collect all layers present
    clayers = sorted({layers[m] for m in members})

    # Stable, human-readable canonical ID
    cid = f"loc_{round(cx / GRID) * GRID:.0f}_{round(cy / GRID) * GRID:.0f}"

    canonical_rows.append({
        "id"    : cid,
        "x"     : round(cx, 3),
        "y"     : round(cy, 3),
        "name"  : cname,
        "layers": ",".join(clayers),   # e.g. "bus,walk" or "metro,walk"
    })

    for m in members:
        remap[ids[m]] = cid

# ── edge remapping + deduplication ───────────────────────────────────────────
edges_df["source"] = edges_df["source"].map(remap).fillna(edges_df["source"])
edges_df["target"] = edges_df["target"].map(remap).fillna(edges_df["target"])

# drop self-loops produced by the merge
n_loops = (edges_df["source"] == edges_df["target"]).sum()
edges_df = edges_df[edges_df["source"] != edges_df["target"]].copy()
print(f"  Self-loops removed       : {n_loops:,}")

# deduplicate (source, target, mode) keeping minimum weight
edges_df = (
    edges_df
    .groupby(["source", "target", "mode"], sort=False)["weight"]
    .min()
    .reset_index()
)

# multi-edge summary: pairs that have edges in more than one mode
multi = (
    edges_df
    .groupby(["source", "target"])["mode"]
    .nunique()
)
print(f"  Final edges              : {len(edges_df):,}")
print(f"  (source,target) pairs with ≥2 modes : {(multi >= 2).sum():,}")

# ── save normalised output ────────────────────────────────────────────────────
canonical_df = pd.DataFrame(canonical_rows)
canonical_df.to_csv("nodes_norm.csv", index=False)
edges_df.to_csv("edges_norm.csv", index=False)

print("\n  Saved -> nodes_norm.csv")
print("  Saved -> edges_norm.csv")

# ── quick sanity check ────────────────────────────────────────────────────────
import networkx as nx

G_norm = nx.MultiGraph()
for _, row in edges_df.iterrows():
    G_norm.add_edge(row["source"], row["target"], mode=row["mode"],
                    weight=row["weight"])

components_norm = list(nx.connected_components(G_norm))
largest_norm    = max(len(c) for c in components_norm)
isolated_norm   = set(canonical_df["id"]) - set(G_norm.nodes())

print(f"\n  Normalised graph summary")
print(f"    Nodes      : {len(canonical_df):,}")
print(f"    Edges      : {G_norm.number_of_edges():,}")
print(f"    Components : {len(components_norm)}")
print(f"    Largest    : {largest_norm:,} nodes")
print(f"    Isolated   : {len(isolated_norm):,}")

# ── example: show a few multi-mode pairs ─────────────────────────────────────
print("\n  Sample multi-mode (source, target) pairs:")
multi_pairs = multi[multi >= 2].reset_index().head(5)
for _, row in multi_pairs.iterrows():
    sub = edges_df[
        (edges_df["source"] == row["source"]) &
        (edges_df["target"] == row["target"])
        ]
    modes = sub[["mode","weight"]].to_dict("records")
    print(f"    {row['source'][:30]} → {row['target'][:30]}")
    for m in modes:
        print(f"      mode={m['mode']:25s}  weight={m['weight']:.1f} m")

print("\n  Normalisation complete.")


# ═══════════════════════════════════════════════════════════════════════════════
# 13 · Visual output — normalised graph map
# ═══════════════════════════════════════════════════════════════════════════════
"""
Four-panel figure saved as  graph_normalised.png
 
  Panel A (large, left)  – Full spatial map
      • Walk edges drawn first as a faint grey web
      • Bus edges in blue, metro edges in red, transfer edges in amber
      • Nodes coloured by which layers were merged into them:
          walk-only        → small grey dot
          bus (±walk)      → medium blue circle
          metro (±walk)    → large red circle
          bus + metro      → gold star  (interchange!)
 
  Panel B (top-right)    – Degree distribution of the normalised graph
      Compares walk-only vs bus/metro-touching nodes.
 
  Panel C (mid-right)    – Edge count breakdown by mode group
 
  Panel D (bottom-right) – Interchange node table
      Lists the top-10 highest-degree interchange nodes (bus+metro present)
      with their stop name and degree.
 
Performance note
----------------
The walk layer typically has 30-80k nodes and 70-150k edges.  Drawing every
edge individually would take minutes and produce an unreadable hairball.
We use LineCollection (vectorised) for edges and scatter (vectorised) for
nodes — the whole figure renders in a few seconds.
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D
import matplotlib.patches as mpatches
import numpy as np
import pandas as pd

print("\n== 8 · Generating normalised graph visualisation =======================")

# ── reload outputs if running standalone ─────────────────────────────────────
try:
    canonical_df  # noqa: F821
    edges_df      # noqa: F821
except NameError:
    canonical_df = pd.read_csv("nodes_norm.csv", dtype=str)
    canonical_df["x"] = canonical_df["x"].astype(float)
    canonical_df["y"] = canonical_df["y"].astype(float)
    edges_df = pd.read_csv("edges_norm.csv", dtype=str)
    edges_df["weight"] = edges_df["weight"].astype(float)

# ── colour / style theme ──────────────────────────────────────────────────────
BG     = "#0D1117"
PANEL  = "#161B22"
GRID_C = "#21262D"

C_WALK     = "#34D399"   # emerald
C_BUS      = "#60A5FA"   # sky blue
C_METRO    = "#F87171"   # rose
C_TRANSFER = "#FBBF24"   # amber
C_XCHANGE  = "#E879F9"   # fuchsia  (bus+metro interchange)

EDGE_ALPHA = {"walk": 0.06, "bus": 0.55, "metro": 0.85, "transfer": 0.40}

# ── node classification ───────────────────────────────────────────────────────
# layers column looks like "bus,walk" or "metro" or "walk" etc.
def has(row, layer):
    return layer in row["layers"].split(",")

canonical_df["has_walk"]  = canonical_df.apply(lambda r: has(r, "walk"),  axis=1)
canonical_df["has_bus"]   = canonical_df.apply(lambda r: has(r, "bus"),   axis=1)
canonical_df["has_metro"] = canonical_df.apply(lambda r: has(r, "metro"), axis=1)
canonical_df["is_xchange"]= canonical_df["has_bus"] & canonical_df["has_metro"]

mask_walk  = canonical_df["has_walk"]  & ~canonical_df["has_bus"] & ~canonical_df["has_metro"]
mask_bus   = canonical_df["has_bus"]   & ~canonical_df["has_metro"]
mask_metro = canonical_df["has_metro"] & ~canonical_df["has_bus"]
mask_xchg  = canonical_df["is_xchange"]

# ── coordinate lookup ─────────────────────────────────────────────────────────
xy = canonical_df.set_index("id")[["x", "y"]]

def get_xy(nid):
    try:
        r = xy.loc[nid]
        return float(r["x"]), float(r["y"])
    except KeyError:
        return None

# ── build edge arrays per mode group ─────────────────────────────────────────
def mode_group(mode: str) -> str:
    if "transfer" in mode:
        return "transfer"
    return mode   # walk / bus / metro

edges_df["group"] = edges_df["mode"].apply(mode_group)

print("  Building edge segment arrays …")
seg_by_group: dict[str, list] = {g: [] for g in ["walk", "bus", "metro", "transfer"]}

for _, row in edges_df.iterrows():
    src = get_xy(row["source"])
    tgt = get_xy(row["target"])
    if src is None or tgt is None:
        continue
    seg_by_group[row["group"]].append([src, tgt])

seg_counts = {g: len(v) for g, v in seg_by_group.items()}
print(f"  Edge segments: { {g: f'{n:,}' for g, n in seg_counts.items()} }")

# ── degree in normalised graph ────────────────────────────────────────────────
deg_series = pd.concat([edges_df["source"], edges_df["target"]]).value_counts()
canonical_df["degree"] = canonical_df["id"].map(deg_series).fillna(0).astype(int)

# ── figure layout ─────────────────────────────────────────────────────────────
fig = plt.figure(figsize=(22, 14), facecolor=BG)
gs  = gridspec.GridSpec(
    3, 2,
    figure=fig,
    left=0.03, right=0.97,
    top=0.93,  bottom=0.04,
    hspace=0.38, wspace=0.28,
    width_ratios=[1.8, 1],
    height_ratios=[1, 1, 1],
)

ax_map  = fig.add_subplot(gs[:, 0])    # Panel A – full map (spans all rows)
ax_deg  = fig.add_subplot(gs[0, 1])    # Panel B – degree distribution
ax_edge = fig.add_subplot(gs[1, 1])    # Panel C – edge counts
ax_tbl  = fig.add_subplot(gs[2, 1])    # Panel D – interchange table

for ax in [ax_map, ax_deg, ax_edge, ax_tbl]:
    ax.set_facecolor(PANEL)
    for sp in ax.spines.values():
        sp.set_color(GRID_C)

# ════════════════════════════════════════════════════════════════════════════
# Panel A · Spatial map
# ════════════════════════════════════════════════════════════════════════════
ax_map.set_title("Normalised Multi-Modal Graph — Porto", color="white",
                 fontsize=14, pad=10, fontweight="bold")

# draw edges (walk first so bus/metro sit on top)
draw_order = [
    ("walk",     C_WALK,     0.4,  EDGE_ALPHA["walk"]),
    ("transfer", C_TRANSFER, 0.9,  EDGE_ALPHA["transfer"]),
    ("bus",      C_BUS,      1.0,  EDGE_ALPHA["bus"]),
    ("metro",    C_METRO,    2.0,  EDGE_ALPHA["metro"]),
]
for group, color, lw, alpha in draw_order:
    segs = seg_by_group[group]
    if not segs:
        continue
    lc = LineCollection(segs, colors=color, linewidths=lw, alpha=alpha, zorder=1)
    ax_map.add_collection(lc)

# draw nodes
def scatter_nodes(mask, color, size, zorder, marker="o", label=""):
    sub = canonical_df[mask]
    if sub.empty:
        return
    ax_map.scatter(sub["x"], sub["y"], s=size, c=color, alpha=0.85,
                   linewidths=0, zorder=zorder, marker=marker, label=label)

scatter_nodes(mask_walk,  C_WALK,    0.3,  2, label=f"Walk-only ({mask_walk.sum():,})")
scatter_nodes(mask_bus,   C_BUS,     12,   3, label=f"Bus stop ({mask_bus.sum():,})")
scatter_nodes(mask_metro, C_METRO,   40,   4, label=f"Metro stop ({mask_metro.sum():,})")
scatter_nodes(mask_xchg,  C_XCHANGE, 90,   5, marker="*",
              label=f"Bus+Metro interchange ({mask_xchg.sum():,})")

# autoscale after LineCollection
ax_map.autoscale_view()
ax_map.set_aspect("equal")
ax_map.set_xlabel("Easting (m, UTM)", color="#9CA3AF", fontsize=9)
ax_map.set_ylabel("Northing (m, UTM)", color="#9CA3AF", fontsize=9)
ax_map.tick_params(colors="#9CA3AF", labelsize=7)

# legend
legend_extras = [
    Line2D([0],[0], color=C_WALK,     lw=1.5, alpha=0.5, label="Walk edge"),
    Line2D([0],[0], color=C_BUS,      lw=1.5, alpha=0.8, label="Bus edge"),
    Line2D([0],[0], color=C_METRO,    lw=2.0, alpha=0.9, label="Metro edge"),
    Line2D([0],[0], color=C_TRANSFER, lw=1.5, alpha=0.7, label="Transfer edge"),
]
handles, labels = ax_map.get_legend_handles_labels()
ax_map.legend(handles + legend_extras, labels + [h.get_label() for h in legend_extras],
              facecolor="#0D1117", labelcolor="white", fontsize=8,
              framealpha=0.85, loc="upper right", markerscale=2.5)

# ════════════════════════════════════════════════════════════════════════════
# Panel B · Degree distribution  (walk-only vs transit-touching)
# ════════════════════════════════════════════════════════════════════════════
ax_deg.set_title("Degree Distribution", color="white", fontsize=11, pad=8)

transit_mask = canonical_df["has_bus"] | canonical_df["has_metro"]
cap = min(int(canonical_df["degree"].quantile(0.995)), 30)

for subset, color, label in [
    (canonical_df[~transit_mask]["degree"], C_WALK,  "Walk-only"),
    (canonical_df[transit_mask]["degree"],  C_BUS,   "Transit-touching"),
]:
    clipped = subset.clip(upper=cap)
    ax_deg.hist(clipped, bins=range(0, cap+2), alpha=0.72, color=color,
                label=label, edgecolor=BG, linewidth=0.3)

ax_deg.set_xlabel("Degree (capped)", color="#9CA3AF", fontsize=8)
ax_deg.set_ylabel("Node count", color="#9CA3AF", fontsize=8)
ax_deg.tick_params(colors="#9CA3AF", labelsize=7)
ax_deg.legend(facecolor=PANEL, labelcolor="white", fontsize=8, framealpha=0.9)
ax_deg.grid(axis="y", color=GRID_C, linewidth=0.4)

# ════════════════════════════════════════════════════════════════════════════
# Panel C · Edge counts by mode group
# ════════════════════════════════════════════════════════════════════════════
ax_edge.set_title("Edge Count by Mode", color="white", fontsize=11, pad=8)

group_counts = edges_df["group"].value_counts().reindex(
    ["walk", "bus", "metro", "transfer"], fill_value=0
)
colors_bar = [C_WALK, C_BUS, C_METRO, C_TRANSFER]
bars = ax_edge.barh(group_counts.index.tolist(), group_counts.values,
                    color=colors_bar, edgecolor=BG, height=0.55)
ax_edge.set_xlabel("Edge count", color="#9CA3AF", fontsize=8)
ax_edge.tick_params(colors="#9CA3AF", labelsize=8)
ax_edge.grid(axis="x", color=GRID_C, linewidth=0.4)
for bar, val in zip(bars, group_counts.values):
    ax_edge.text(val + max(group_counts.values)*0.01,
                 bar.get_y() + bar.get_height()/2,
                 f"{val:,}", va="center", color="#E5E7EB", fontsize=8)

# ════════════════════════════════════════════════════════════════════════════
# Panel D · Top interchange nodes table
# ════════════════════════════════════════════════════════════════════════════
ax_tbl.set_title("Top Interchange Nodes (Bus + Metro)", color="white",
                 fontsize=11, pad=8)
ax_tbl.axis("off")

top_xchg = (
    canonical_df[mask_xchg | mask_metro | mask_bus]
    .nlargest(10, "degree")[["name", "layers", "degree"]]
    .reset_index(drop=True)
)

if top_xchg.empty:
    ax_tbl.text(0.5, 0.5, "No interchange nodes found",
                ha="center", va="center", color="#9CA3AF", fontsize=9,
                transform=ax_tbl.transAxes)
else:
    col_labels = ["Name", "Layers", "Degree"]
    cell_text  = [
        [
            (r["name"][:22] + "…" if len(str(r["name"])) > 23 else str(r["name"])) or "—",
            r["layers"],
            str(r["degree"]),
            ]
        for _, r in top_xchg.iterrows()
    ]
    tbl = ax_tbl.table(
        cellText=cell_text,
        colLabels=col_labels,
        loc="center",
        cellLoc="left",
    )
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(8)
    tbl.scale(1, 1.45)

    # Style header
    for j in range(len(col_labels)):
        tbl[(0, j)].set_facecolor("#1F2937")
        tbl[(0, j)].set_text_props(color=C_TRANSFER, fontweight="bold")
    # Style rows
    for i in range(1, len(cell_text)+1):
        for j in range(len(col_labels)):
            tbl[(i, j)].set_facecolor(PANEL if i % 2 == 0 else "#1A2233")
            tbl[(i, j)].set_text_props(color="#E5E7EB")
            tbl[(i, j)].set_edgecolor(GRID_C)

# ── footer ────────────────────────────────────────────────────────────────────
n_nodes   = len(canonical_df)
n_edges   = len(edges_df)
n_xchg    = int(mask_xchg.sum())
n_bus_stp = int(mask_bus.sum())
n_met_stp = int(mask_metro.sum())

fig.text(
    0.5, 0.005,
    f"Canonical nodes: {n_nodes:,}  |  Edges: {n_edges:,}  |  "
    f"Bus stops: {n_bus_stp:,}  |  Metro stops: {n_met_stp:,}  |  "
    f"Bus+Metro interchanges: {n_xchg:,}",
    ha="center", color="#6B7280", fontsize=8.5,
)
fig.suptitle(
    "Porto Public-Transport Graph — Post-Normalisation Diagnostics",
    color="white", fontsize=15, fontweight="bold", y=0.975,
)

out_path = "graph_normalised.png"
plt.savefig(out_path, dpi=150, bbox_inches="tight", facecolor=BG)
plt.close(fig)
print(f"  Saved -> {out_path}")
print("  Visualisation complete.")