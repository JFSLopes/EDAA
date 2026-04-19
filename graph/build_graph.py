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