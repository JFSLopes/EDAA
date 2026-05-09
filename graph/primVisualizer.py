import pandas as pd
import matplotlib.pyplot as plt
import matplotlib
from matplotlib.collections import LineCollection
from pathlib import Path

matplotlib.use("Agg")

DIR = Path(__file__).parent

BG, PANEL = "#0D1117", "#161B22"
COLOR_MAP  = {"walk": "#34D399", "bus": "#60A5FA", "metro": "#F87171"}

edges = pd.read_csv(DIR / "algorithm_edges.csv")

fig, ax = plt.subplots(figsize=(14, 14), facecolor=BG)
ax.set_facecolor(PANEL)

# Use real nodes file as background if it exists and is small enough
nodes_path = DIR / "nodes_clipped.csv"
if nodes_path.exists():
    nodes = pd.read_csv(nodes_path)
    if len(nodes) < 50000:
        ax.scatter(nodes["x"], nodes["y"], s=0.3, c="#34D399", alpha=0.15, linewidths=0)

for mode, color in COLOR_MAP.items():
    subset = edges[edges["mode"] == mode]
    if subset.empty:
        continue
    segs = [[(r.x1, r.y1), (r.x2, r.y2)] for _, r in subset.iterrows()]
    ax.add_collection(LineCollection(segs, colors=color, linewidths=1.0,
                                     alpha=0.85, label=mode.capitalize()))

ax.autoscale_view()
ax.set_aspect("equal")
ax.set_title("Prim Minimum Spanning Tree", color="white", fontsize=14, pad=10)
ax.tick_params(colors="#9CA3AF")
ax.legend(facecolor="#161B22", labelcolor="white", fontsize=9, loc="upper right")
for sp in ax.spines.values():
    sp.set_color("#21262D")

plt.savefig(DIR / "prim_tree.png", dpi=150, bbox_inches="tight", facecolor=BG)
plt.close()
print(f"Saved → {DIR / 'prim_tree.png'}")