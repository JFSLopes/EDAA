#!/usr/bin/env python3
"""
Create a GIF explaining a Quadtree range query with pruning.

Output:
    visual/quadtree_pruning.gif

Usage:
    python3 visual_quadtree.py
    python3 visual_quadtree.py --fps 0.8 --output visual/quadtree_pruning.gif
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter
from matplotlib.patches import Circle, Rectangle

POINTS = [
    ("A", 12, 82), ("B", 20, 65), ("C", 32, 78),
    ("D", 18, 18), ("E", 34, 28),
    ("F", 68, 74), ("G", 82, 86), ("H", 74, 58),
    ("I", 64, 22), ("J", 86, 18),
]
QUERY = ("Q", 70, 62)
RADIUS = 19

@dataclass
class QuadNode:
    name: str
    xmin: float
    ymin: float
    xmax: float
    ymax: float
    points: list[tuple[str, float, float]]
    children: list["QuadNode"] = field(default_factory=list)
    def is_leaf(self) -> bool:
        return len(self.children) == 0


def build_demo_tree() -> QuadNode:
    root = QuadNode("Root", 0, 0, 100, 100, POINTS)
    nw = QuadNode("NW", 0, 50, 50, 100, [p for p in POINTS if p[1] < 50 and p[2] >= 50])
    ne = QuadNode("NE", 50, 50, 100, 100, [p for p in POINTS if p[1] >= 50 and p[2] >= 50])
    sw = QuadNode("SW", 0, 0, 50, 50, [p for p in POINTS if p[1] < 50 and p[2] < 50])
    se = QuadNode("SE", 50, 0, 100, 50, [p for p in POINTS if p[1] >= 50 and p[2] < 50])
    root.children = [nw, ne, sw, se]
    ne.children = [
        QuadNode("NE-NW", 50, 75, 75, 100, [p for p in ne.points if p[1] < 75 and p[2] >= 75]),
        QuadNode("NE-NE", 75, 75, 100, 100, [p for p in ne.points if p[1] >= 75 and p[2] >= 75]),
        QuadNode("NE-SW", 50, 50, 75, 75, [p for p in ne.points if p[1] < 75 and p[2] < 75]),
        QuadNode("NE-SE", 75, 50, 100, 75, [p for p in ne.points if p[1] >= 75 and p[2] < 75]),
    ]
    return root


def rect_intersects_circle(node: QuadNode, qx: float, qy: float, r: float) -> bool:
    closest_x = min(max(qx, node.xmin), node.xmax)
    closest_y = min(max(qy, node.ymin), node.ymax)
    dx = qx - closest_x
    dy = qy - closest_y
    return dx * dx + dy * dy <= r * r


def point_in_radius(p, qx, qy, r):
    _, x, y = p
    return (x - qx) ** 2 + (y - qy) ** 2 <= r * r


def traversal_steps(root: QuadNode):
    qx, qy = QUERY[1], QUERY[2]
    steps = []
    stack = [root]
    visited = []
    pruned = []
    accepted_points = []
    steps.append(("Start query", None, list(visited), list(pruned), list(accepted_points),
                  "Search neighbours of Q inside the blue radius."))
    while stack:
        node = stack.pop()
        if not rect_intersects_circle(node, qx, qy, RADIUS):
            pruned.append(node.name)
            steps.append((f"Prune {node.name}", node.name, list(visited), list(pruned), list(accepted_points),
                          f"{node.name} box does not intersect the search circle. Skip whole subtree."))
            continue
        visited.append(node.name)
        steps.append((f"Visit {node.name}", node.name, list(visited), list(pruned), list(accepted_points),
                      f"{node.name} intersects the search circle. Continue."))
        if node.is_leaf():
            for p in node.points:
                if point_in_radius(p, qx, qy, RADIUS):
                    accepted_points.append(p[0])
                    steps.append((f"Accept point {p[0]}", node.name, list(visited), list(pruned), list(accepted_points),
                                  f"{p[0]} is inside the radius. It is a neighbour."))
                else:
                    steps.append((f"Reject point {p[0]}", node.name, list(visited), list(pruned), list(accepted_points),
                                  f"{p[0]} is in a visited box but outside the radius."))
        else:
            for child in reversed(node.children):
                stack.append(child)
    steps.append(("Done", None, list(visited), list(pruned), list(accepted_points),
                  "Only points in non-pruned boxes are checked by distance."))
    return steps


def iter_nodes(root: QuadNode):
    yield root
    for child in root.children:
        yield from iter_nodes(child)


def assign_tree_positions(root: QuadNode):
    levels = {}

    def collect(node, depth=0):
        levels.setdefault(depth, []).append(node)
        for child in node.children:
            collect(child, depth + 1)

    collect(root)

    positions = {}

    for depth, nodes in levels.items():
        for i, node in enumerate(nodes):
            x = (i + 1) / (len(nodes) + 1)
            y = 0.96 - depth * 0.25
            positions[node.name] = (x, y)

    return positions


def draw_tree(ax, root: QuadNode, current, visited, pruned):
    ax.set_title("Quadtree traversal", fontsize=14, fontweight="bold")
    ax.axis("off")

    # Add padding so large circular nodes are not clipped.
    ax.set_xlim(-0.08, 1.08)
    ax.set_ylim(0.28, 1.12)

    positions = assign_tree_positions(root)

    for node in iter_nodes(root):
        x1, y1 = positions[node.name]
        for child in node.children:
            x2, y2 = positions[child.name]
            ax.plot([x1, x2], [y1, y2], color="#888888", linewidth=1.5, zorder=1)

    for node in iter_nodes(root):
        x, y = positions[node.name]

        if node.name == current:
            color = "#4da3ff"
        elif node.name in pruned:
            color = "#ff8a80"
        elif node.name in visited:
            color = "#5cb85c"
        else:
            color = "#eeeeee"

        ax.scatter(
            [x],
            [y],
            s=1700,
            color=color,
            edgecolor="black",
            zorder=3,
            clip_on=False,
        )

        ax.text(
            x,
            y + 0.015,
            node.name,
            ha="center",
            va="center",
            fontsize=10,
            fontweight="bold",
            zorder=4,
            clip_on=False,
            )

        ax.text(
            x,
            y - 0.035,
            f"{len(node.points)} pts",
            ha="center",
            va="center",
            fontsize=8,
            zorder=4,
            clip_on=False,
            )


def draw_space(ax, root: QuadNode, current, visited, pruned, accepted):
    ax.set_title("Spatial subdivision", fontsize=14, fontweight="bold")
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    for node in iter_nodes(root):
        if not node.is_leaf() and node.name != "Root":
            continue
        if node.name == current:
            edge, lw = "#4da3ff", 4
        elif node.name in pruned:
            edge, lw = "#ff8a80", 3
        elif node.name in visited:
            edge, lw = "#5cb85c", 3
        else:
            edge, lw = "#999999", 1.5
        ax.add_patch(Rectangle((node.xmin, node.ymin), node.xmax-node.xmin, node.ymax-node.ymin,
                               fill=False, edgecolor=edge, linewidth=lw))
        ax.text(node.xmin+2, node.ymax-5, node.name, fontsize=8, color=edge)
    ax.plot([50,50], [0,100], color="#aaaaaa", linewidth=1)
    ax.plot([0,100], [50,50], color="#aaaaaa", linewidth=1)
    ax.plot([75,75], [50,100], color="#aaaaaa", linewidth=1)
    ax.plot([50,100], [75,75], color="#aaaaaa", linewidth=1)
    qx, qy = QUERY[1], QUERY[2]
    ax.add_patch(Circle((qx, qy), RADIUS, fill=False, edgecolor="#1f77b4", linewidth=2.5))
    ax.scatter([qx], [qy], s=180, color="#1f77b4", edgecolor="black", zorder=4)
    ax.text(qx+2, qy+2, "Q", fontsize=12, fontweight="bold")
    for name, x, y in POINTS:
        color = "#5cb85c" if name in accepted else "#eeeeee"
        size = 170 if name in accepted else 110
        ax.scatter([x], [y], s=size, color=color, edgecolor="black", zorder=3)
        ax.text(x+1.5, y+1.5, name, fontsize=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="visual/quadtree_pruning.gif")
    parser.add_argument("--fps", type=float, default=0.8)
    args = parser.parse_args()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    root = build_demo_tree()
    steps = traversal_steps(root)
    fig, axes = plt.subplots(1, 2, figsize=(13, 6), facecolor="white")
    fig.suptitle("Quadtree range search: visit useful boxes, prune impossible boxes", fontsize=16, fontweight="bold")
    msg = [None]
    def update(i):
        title, current, visited, pruned, accepted, note = steps[i]
        for ax in axes:
            ax.clear()
        draw_tree(axes[0], root, current, visited, pruned)
        draw_space(axes[1], root, current, visited, pruned, accepted)
        if msg[0] is not None:
            msg[0].remove()
        msg[0] = fig.text(0.5, 0.02, f"{title}: {note}", ha="center", fontsize=11,
                          bbox=dict(boxstyle="round,pad=0.4", facecolor="white", edgecolor="black"))
    interval_ms = 1000.0 / max(args.fps, 0.1)
    anim = FuncAnimation(fig, update, frames=len(steps), interval=interval_ms, repeat=True)
    anim.save(output, writer=PillowWriter(fps=max(args.fps, 0.1)))
    plt.close(fig)
    print(f"[ok] wrote {output}")

if __name__ == "__main__":
    main()
