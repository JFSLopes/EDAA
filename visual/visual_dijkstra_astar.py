#!/usr/bin/env python3
"""
Create a side-by-side GIF showing Dijkstra vs A* on a small coordinate graph.

Graph:
    S (0, 0)
    A (1, -1)
    B (2, -2)
    C (3, -3)
    T (4, 4)

Edges:
    S - T
    S - A
    A - B
    B - C
    C - T

Dijkstra explores A, B, C first because their known distance from S is small.
A* goes directly to T because f = g + h is smallest for T.

Output:
    visual/dijkstra_vs_astar.gif

Usage:
    python3 visual_dijkstra_astar.py
    python3 visual_dijkstra_astar.py --fps 0.7
"""

from __future__ import annotations

import argparse
import heapq
import math
import textwrap
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter
from matplotlib.patches import Rectangle


INF = float("inf")


@dataclass
class Step:
    title: str
    g: dict[str, float]
    queue: list[tuple[float, str]]
    processed: set[str]
    current: str | None
    relaxed_edge: tuple[str, str] | None
    note: str


def fmt_value(x: float) -> str:
    if math.isinf(x):
        return "INF"
    return f"{x:.2f}"


def make_graph():
    pos = {
        "S": (0, 0),
        "A": (1.5, 0),
        "B": (3, 0),
        "T": (4, 4),
    }

    edges = [
        ("S", "T"),
        ("S", "A"),
        ("A", "B"),
        ("B", "T"),
    ]

    adj = {node: [] for node in pos}

    for u, v in edges:
        w = euclidean(pos[u], pos[v])
        adj[u].append((v, w))
        adj[v].append((u, w))

    return pos, adj, edges


def euclidean(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def heuristic(pos: dict[str, tuple[float, float]], node: str, target: str) -> float:
    return euclidean(pos[node], pos[target])


def sorted_queue(heap: list[tuple[float, int, str]]) -> list[tuple[float, str]]:
    return [(key, node) for key, _, node in sorted(heap)]


def run_search(kind: str) -> list[Step]:
    pos, adj, _ = make_graph()
    start = "S"
    target = "T"

    g = {node: INF for node in pos}
    g[start] = 0.0

    processed: set[str] = set()
    heap: list[tuple[float, int, str]] = []
    counter = 0

    def priority(node: str) -> float:
        if kind == "astar":
            return g[node] + heuristic(pos, node, target)
        return g[node]

    heapq.heappush(heap, (priority(start), counter, start))

    steps = [
        Step(
            title="Initial state",
            g=dict(g),
            queue=sorted_queue(heap),
            processed=set(processed),
            current=None,
            relaxed_edge=None,
            note="Start at S. All other distances are INF.",
        )
    ]

    while heap:
        _, _, u = heapq.heappop(heap)

        if u in processed:
            continue

        processed.add(u)

        steps.append(
            Step(
                title=f"Process {u}",
                g=dict(g),
                queue=sorted_queue(heap),
                processed=set(processed),
                current=u,
                relaxed_edge=None,
                note=(
                    f"{kind.upper()} removes {u} from the priority queue. "
                    "If this is T, the algorithm stops."
                ),
            )
        )

        if u == target:
            steps.append(
                Step(
                    title="Target reached",
                    g=dict(g),
                    queue=sorted_queue(heap),
                    processed=set(processed),
                    current=u,
                    relaxed_edge=None,
                    note=f"Reached T. Final shortest distance is {fmt_value(g[target])}.",
                )
            )
            break

        for v, w in adj[u]:
            if v in processed:
                continue

            new_g = g[u] + w

            if new_g < g[v]:
                g[v] = new_g
                counter += 1
                heapq.heappush(heap, (priority(v), counter, v))

                if kind == "astar":
                    h = heuristic(pos, v, target)
                    msg = (
                        f"Relax {u}->{v}: g={fmt_value(g[v])}, "
                        f"h={fmt_value(h)}, f={fmt_value(priority(v))}. "
                        "A* prefers the node with smallest g+h."
                    )
                else:
                    msg = (
                        f"Relax {u}->{v}: g={fmt_value(g[v])}. "
                        "Dijkstra only considers distance already travelled."
                    )

                steps.append(
                    Step(
                        title=f"Relax {u} → {v}",
                        g=dict(g),
                        queue=sorted_queue(heap),
                        processed=set(processed),
                        current=u,
                        relaxed_edge=(u, v),
                        note=msg,
                    )
                )

    return steps


def draw_priority_queue(ax, step: Step, kind: str, pos: dict[str, tuple[float, float]]) -> None:
    ax.text(
        0.5,
        1.33,
        "Priority queue",
        transform=ax.transAxes,
        ha="center",
        va="center",
        fontsize=16,
        fontweight="bold",
        clip_on=False,
    )

    if not step.queue:
        ax.text(
            0.5,
            1.19,
            "empty",
            transform=ax.transAxes,
            ha="center",
            va="center",
            fontsize=13,
            bbox=dict(boxstyle="round,pad=0.35", facecolor="#f7f7f7", edgecolor="black"),
            clip_on=False,
        )
        return

    queue = step.queue[:4]

    cell_w = 0.23
    cell_h = 0.145
    gap = 0.022

    total_w = len(queue) * cell_w + (len(queue) - 1) * gap
    start_x = 0.5 - total_w / 2
    y = 1.12

    for i, (key, node) in enumerate(queue):
        x = start_x + i * (cell_w + gap)

        rect = Rectangle(
            (x, y),
            cell_w,
            cell_h,
            transform=ax.transAxes,
            facecolor="#f7f7f7",
            edgecolor="#333333",
            linewidth=1.6,
            clip_on=False,
        )
        ax.add_patch(rect)

        if kind == "astar":
            h = heuristic(pos, node, "T")
            text = f"{node}\ng={fmt_value(step.g[node])}  h={fmt_value(h)}\nf={fmt_value(key)}"
        else:
            text = f"{node}\ng={fmt_value(step.g[node])}\nkey={fmt_value(key)}"

        ax.text(
            x + cell_w / 2,
            y + cell_h / 2,
            text,
            transform=ax.transAxes,
            ha="center",
            va="center",
            fontsize=10.5,
            fontweight="bold",
            linespacing=1.15,
            clip_on=False,
            )


def draw_grid(ax):
    ax.set_xticks(range(0, 7))
    ax.set_yticks(range(0, 5))
    ax.grid(True, color="#dddddd", linewidth=0.8)
    ax.set_axisbelow(True)


def draw_panel(ax, title: str, step: Step, kind: str):
    pos, adj, edges = make_graph()

    ax.set_title(title, fontsize=16, fontweight="bold", pad=18)
    ax.set_xlim(-0.6, 4.6)
    ax.set_ylim(-0.6, 4.6)
    ax.set_aspect("equal")

    draw_grid(ax)
    draw_priority_queue(ax, step, kind, pos)

    ax.set_xlabel("x")
    ax.set_ylabel("y")

    for u, v in edges:
        x1, y1 = pos[u]
        x2, y2 = pos[v]
        w = euclidean(pos[u], pos[v])

        color = "#777777"
        linewidth = 2.0

        if step.relaxed_edge == (u, v) or step.relaxed_edge == (v, u):
            color = "#1f77b4"
            linewidth = 4.5

        ax.plot([x1, x2], [y1, y2], color=color, linewidth=linewidth, zorder=1)

        ax.text(
            (x1 + x2) / 2,
            (y1 + y2) / 2,
            fmt_value(w),
            fontsize=9,
            ha="center",
            va="center",
            bbox=dict(boxstyle="round,pad=0.15", facecolor="white", edgecolor="none"),
            zorder=2,
            )

    queued_nodes = {node for _, node in step.queue}

    for node, (x, y) in pos.items():
        if node == step.current:
            color = "#4da3ff"
        elif node in step.processed:
            color = "#5cb85c"
        elif node in queued_nodes:
            color = "#ffd84d"
        else:
            color = "#eeeeee"

        ax.scatter(
            [x],
            [y],
            s=2100,
            color=color,
            edgecolor="black",
            linewidth=1.8,
            zorder=3,
        )

        ax.text(
            x,
            y + 0.16,
            node,
            ha="center",
            va="center",
            fontsize=17,
            fontweight="bold",
            zorder=4,
            )

        ax.text(
            x,
            y - 0.16,
            f"g={fmt_value(step.g[node])}",
            ha="center",
            va="center",
            fontsize=10,
            zorder=4,
            )

    wrapped_note = textwrap.fill(step.note, width=58)

    ax.text(
        0.5,
        -0.16,
        wrapped_note,
        transform=ax.transAxes,
        ha="center",
        va="top",
        fontsize=13,
        linespacing=1.25,
        bbox=dict(boxstyle="round,pad=0.45", facecolor="white", edgecolor="#999999"),
        clip_on=False,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="visual/dijkstra_vs_astar.gif")
    parser.add_argument("--fps", type=float, default=0.7)
    args = parser.parse_args()

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)

    dijkstra_steps = run_search("dijkstra")
    astar_steps = run_search("astar")
    frame_count = max(len(dijkstra_steps), len(astar_steps))

    fig, axes = plt.subplots(1, 2, figsize=(16, 9.2), facecolor="white")

    fig.suptitle(
        "Dijkstra vs A*",
        fontsize=18,
        fontweight="bold",
        y=0.96,
    )
    fig.subplots_adjust(top=0.74, bottom=0.18, wspace=0.18)

    def update(i):
        for ax in axes:
            ax.clear()

        ds = dijkstra_steps[min(i, len(dijkstra_steps) - 1)]
        ast = astar_steps[min(i, len(astar_steps) - 1)]

        draw_panel(axes[0], f"Dijkstra — {ds.title}", ds, "dijkstra")
        draw_panel(axes[1], f"A* — {ast.title}", ast, "astar")

    interval_ms = 1000.0 / max(args.fps, 0.1)

    anim = FuncAnimation(
        fig,
        update,
        frames=frame_count,
        interval=interval_ms,
        repeat=True,
    )

    anim.save(output, writer=PillowWriter(fps=max(args.fps, 0.1)))
    plt.close(fig)

    print(f"[ok] wrote {output}")


if __name__ == "__main__":
    main()