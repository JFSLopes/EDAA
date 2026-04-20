import pandas as pd
import matplotlib.pyplot as plt
import matplotlib
from matplotlib.collections import LineCollection

matplotlib.use("Agg")

BG, PANEL = "#0D1117", "#161B22"
COLOR_MAP  = {"walk": "#34D399", "bus": "#60A5FA", "metro": "#F87171"}

nodes = pd.read_csv("nodes_clipped.csv")
prim  = pd.read_csv("prim_edges.csv")

fig, ax = plt.subplots(figsize=(14, 14), facecolor=BG)
ax.set_facecolor(PANEL)

# Base map
ax.scatter(nodes["x"], nodes["y"], s=0.3, c="#34D399", alpha=0.15, linewidths=0)

# Prim edges grouped by mode so we can use LineCollection per mode
for mode, color in COLOR_MAP.items():
    subset = prim[prim["mode"] == mode]
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

plt.savefig("prim_tree.png", dpi=150, bbox_inches="tight", facecolor=BG)
plt.close()
print("Saved → prim_tree.png")