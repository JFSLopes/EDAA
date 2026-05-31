#!/usr/bin/env python3
"""
Create a GIF explaining the Welsh-Powell graph coloring algorithm.

This version shows only Welsh-Powell and follows the standard procedure:

1. Compute the degree of each vertex.
2. Sort vertices by descending degree.
3. Pick the first uncoloured vertex and assign a new colour.
4. Move down the ordered list and give the same colour to every uncoloured
   vertex that is not adjacent to any vertex already coloured with this colour.
5. Repeat with a new colour until all vertices are coloured.

Output:
    visual/welsh_powell.gif

Usage:
    python3 visual_welsh_powell.py
    python3 visual_welsh_powell.py --fps 0.8
    python3 visual_welsh_powell.py --fps 0.5 --output visual/welsh_powell.gif
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter


# Graph chosen to make the Welsh-Powell passes clear.
#
# Degrees:
#   B = 4
#   D = 4
#   A = 3
#   C = 3
#   E = 2
#   F = 2
#
# Descending degree order:
#   B, D, A, C, E, F
#
# Welsh-Powell will color groups, not just one vertex at a time.
NODES = ["A", "B", "C", "D", "E", "F"]

POS = {
    "A": (0.0, 1.2),
    "B": (1.0, 2.0),
    "C": (2.0, 1.2),
    "D": (1.0, 0.2),
    "E": (0.0, -0.6),
    "F": (2.0, -0.6),
}

EDGES = [
    ("A", "B"),
    ("A", "D"),
    ("A", "E"),
    ("B", "C"),
    ("B", "D"),
    ("B", "E"),
    ("B", "F"),
    ("C", "D"),
    ("C", "F"),
    ("D", "E"),
    ("D", "F"),
]

COLOR_NAMES = ["Colour 1", "Colour 2", "Colour 3", "Colour 4"]
PALETTE = {
    None: "#eeeeee",
    0: "#ff9999",
    1: "#99ccff",
    2: "#99dd99",
    3: "#ffd966",
}


@dataclass
class Step:
    title: str
    assignment: dict[str, int | None]
    current_vertex: str | None
    current_colour: int | None
    active_colour_vertices: set[str]
    rejected_vertex: str | None
    rejected_reason_vertices: set[str]
    ordered_vertices: list[str]
    note: str


def degree_map() -> dict[str, int]:
    degree = {node: 0 for node in NODES}
    for u, v in EDGES:
        degree[u] += 1
        degree[v] += 1
    return degree


def ordered_by_degree() -> list[str]:
    deg = degree_map()
    return sorted(NODES, key=lambda node: (-deg[node], node))


def adjacent(u: str, v: str) -> bool:
    return (u, v) in EDGES or (v, u) in EDGES


def neighbours(node: str) -> set[str]:
    out = set()
    for u, v in EDGES:
        if u == node:
            out.add(v)
        elif v == node:
            out.add(u)
    return out


def can_use_colour(
        node: str,
        colour_vertices: set[str],
) -> tuple[bool, set[str]]:
    conflicts = {v for v in colour_vertices if adjacent(node, v)}
    return len(conflicts) == 0, conflicts


def build_steps() -> list[Step]:
    order = ordered_by_degree()
    deg = degree_map()
    assignment = {node: None for node in NODES}
    steps: list[Step] = []

    steps.append(
        Step(
            title="Step 1 — compute degrees",
            assignment=dict(assignment),
            current_vertex=None,
            current_colour=None,
            active_colour_vertices=set(),
            rejected_vertex=None,
            rejected_reason_vertices=set(),
            ordered_vertices=[],
            note="Compute the degree of each vertex: how many neighbours each vertex has.",
        )
    )

    steps.append(
        Step(
            title="Step 2 — sort vertices",
            assignment=dict(assignment),
            current_vertex=None,
            current_colour=None,
            active_colour_vertices=set(),
            rejected_vertex=None,
            rejected_reason_vertices=set(),
            ordered_vertices=list(order),
            note="Sort vertices by descending degree: "
                 + " → ".join(f"{v}(deg={deg[v]})" for v in order),
        )
    )

    colour = 0

    while any(assignment[v] is None for v in order):
        colour_vertices: set[str] = set()

        # First uncoloured vertex starts the new colour class.
        first = next(v for v in order if assignment[v] is None)
        assignment[first] = colour
        colour_vertices.add(first)

        steps.append(
            Step(
                title=f"Start {COLOR_NAMES[colour]}",
                assignment=dict(assignment),
                current_vertex=first,
                current_colour=colour,
                active_colour_vertices=set(colour_vertices),
                rejected_vertex=None,
                rejected_reason_vertices=set(),
                ordered_vertices=list(order),
                note=(
                    f"Pick the first uncoloured vertex in the ordered list: {first}. "
                    f"Assign {COLOR_NAMES[colour]}."
                ),
            )
        )

        # Move down the ordered list and try to add compatible vertices
        # to the same colour class.
        for node in order:
            if assignment[node] is not None:
                continue

            ok, conflicts = can_use_colour(node, colour_vertices)

            if ok:
                assignment[node] = colour
                colour_vertices.add(node)

                steps.append(
                    Step(
                        title=f"Accept {node} for {COLOR_NAMES[colour]}",
                        assignment=dict(assignment),
                        current_vertex=node,
                        current_colour=colour,
                        active_colour_vertices=set(colour_vertices),
                        rejected_vertex=None,
                        rejected_reason_vertices=set(),
                        ordered_vertices=list(order),
                        note=(
                            f"{node} is not adjacent to any vertex already using "
                            f"{COLOR_NAMES[colour]}. Give it the same colour."
                        ),
                    )
                )
            else:
                steps.append(
                    Step(
                        title=f"Reject {node} for {COLOR_NAMES[colour]}",
                        assignment=dict(assignment),
                        current_vertex=node,
                        current_colour=colour,
                        active_colour_vertices=set(colour_vertices),
                        rejected_vertex=node,
                        rejected_reason_vertices=set(conflicts),
                        ordered_vertices=list(order),
                        note=(
                            f"{node} cannot use {COLOR_NAMES[colour]} because it is adjacent to "
                            f"{', '.join(sorted(conflicts))}, already coloured with this colour."
                        ),
                    )
                )

        steps.append(
            Step(
                title=f"Finish {COLOR_NAMES[colour]}",
                assignment=dict(assignment),
                current_vertex=None,
                current_colour=colour,
                active_colour_vertices=set(colour_vertices),
                rejected_vertex=None,
                rejected_reason_vertices=set(),
                ordered_vertices=list(order),
                note=(
                        f"{COLOR_NAMES[colour]} class is complete: "
                        + ", ".join(sorted(colour_vertices))
                        + ". Now start a new colour for the remaining uncoloured vertices."
                ),
            )
        )

        colour += 1

    used = max(c for c in assignment.values() if c is not None) + 1
    steps.append(
        Step(
            title="Done",
            assignment=dict(assignment),
            current_vertex=None,
            current_colour=None,
            active_colour_vertices=set(),
            rejected_vertex=None,
            rejected_reason_vertices=set(),
            ordered_vertices=list(order),
            note=(
                f"All vertices are coloured. Welsh-Powell used {used} colours. "
                "This is a heuristic: it is fast, but it does not guarantee the chromatic number for every graph."
            ),
        )
    )

    return steps


def draw_graph(ax, step: Step) -> None:
    ax.set_title("Graph colouring", fontsize=15, fontweight="bold")
    ax.set_xlim(-0.45, 2.45)
    ax.set_ylim(-0.95, 2.35)
    ax.set_aspect("equal")
    ax.axis("off")

    conflict_edges = set()
    if step.rejected_vertex is not None:
        for other in step.rejected_reason_vertices:
            conflict_edges.add(tuple(sorted((step.rejected_vertex, other))))

    for u, v in EDGES:
        x1, y1 = POS[u]
        x2, y2 = POS[v]

        is_reject_edge = tuple(sorted((u, v))) in conflict_edges
        same_active_colour = (
                u in step.active_colour_vertices
                and v in step.active_colour_vertices
                and step.assignment[u] == step.assignment[v]
                and step.assignment[u] is not None
        )

        if is_reject_edge:
            color = "#cc3333"
            lw = 4
        elif same_active_colour:
            # This should not happen in a correct colouring. Kept as a safety visual.
            color = "#cc3333"
            lw = 4
        else:
            color = "#777777"
            lw = 1.6

        ax.plot([x1, x2], [y1, y2], color=color, linewidth=lw, zorder=1)

    deg = degree_map()

    for node in NODES:
        x, y = POS[node]
        c = step.assignment[node]
        fill = PALETTE[c]

        if node == step.current_vertex and node == step.rejected_vertex:
            edge = "#cc3333"
            lw = 4
        elif node == step.current_vertex:
            edge = "#1f77b4"
            lw = 4
        elif node in step.active_colour_vertices:
            edge = "#5cb85c"
            lw = 3
        elif node in step.rejected_reason_vertices:
            edge = "#cc3333"
            lw = 3
        else:
            edge = "black"
            lw = 1.5

        ax.scatter([x], [y], s=1250, color=fill, edgecolor=edge, linewidth=lw, zorder=3)
        ax.text(x, y + 0.08, node, ha="center", va="center", fontsize=15, fontweight="bold", zorder=4)
        ax.text(x, y - 0.06, f"deg={deg[node]}", ha="center", va="center", fontsize=8.5, zorder=4)

        if c is not None:
            ax.text(
                x,
                y - 0.22,
                COLOR_NAMES[c],
                ha="center",
                va="center",
                fontsize=8,
                zorder=4,
                )


def draw_order_panel(ax, step: Step) -> None:
    ax.set_title("Ordered vertex list", fontsize=15, fontweight="bold")
    ax.axis("off")
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)

    order = step.ordered_vertices or ordered_by_degree()
    deg = degree_map()

    y_start = 0.88
    row_h = 0.12

    ax.text(0.05, 0.96, "Descending degree order", fontsize=14, fontweight="bold")

    for i, node in enumerate(order):
        y = y_start - i * row_h
        c = step.assignment[node]

        if node == step.current_vertex and node == step.rejected_vertex:
            edge = "#cc3333"
            lw = 3
            status = "rejected for current colour"
        elif node == step.current_vertex:
            edge = "#1f77b4"
            lw = 3
            status = "checking now"
        elif c is None:
            edge = "#999999"
            lw = 1.2
            status = "uncoloured"
        else:
            edge = "#5cb85c"
            lw = 2
            status = COLOR_NAMES[c]

        rect = plt.Rectangle(
            (0.05, y - 0.045),
            0.90,
            0.085,
            facecolor=PALETTE[c],
            edgecolor=edge,
            linewidth=lw,
        )
        ax.add_patch(rect)

        ax.text(0.10, y, f"{i + 1}. {node}", ha="left", va="center", fontsize=13, fontweight="bold")
        ax.text(0.32, y, f"degree={deg[node]}", ha="left", va="center", fontsize=12)
        ax.text(0.60, y, status, ha="left", va="center", fontsize=11)

    if step.current_colour is not None:
        active = ", ".join(sorted(step.active_colour_vertices)) or "none yet"
        ax.text(
            0.5,
            0.08,
            f"Current colour class: {COLOR_NAMES[step.current_colour]} = {{{active}}}",
            ha="center",
            va="center",
            fontsize=12,
            bbox=dict(boxstyle="round,pad=0.35", facecolor="white", edgecolor="black"),
        )


def draw_legend(ax) -> None:
    ax.axis("off")
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)

    items = [
        ("#eeeeee", "Uncoloured"),
        ("#ff9999", "Colour 1"),
        ("#99ccff", "Colour 2"),
        ("#99dd99", "Colour 3"),
        ("#ffd966", "Colour 4"),
    ]

    x = 0.08
    for color, label in items:
        ax.scatter([x], [0.5], s=420, color=color, edgecolor="black")
        ax.text(x + 0.045, 0.5, label, va="center", fontsize=13)
        x += 0.18

    ax.text(
        0.5,
        0.12,
        "Blue outline = vertex being checked | Green outline = vertices already assigned to current colour | Red edge = conflict",
        ha="center",
        fontsize=11,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="visual/welsh_powell.gif")
    parser.add_argument("--fps", type=float, default=0.8, help="Frames per second. Lower is slower.")
    args = parser.parse_args()

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)

    steps = build_steps()

    fig = plt.figure(figsize=(14, 8), facecolor="white")
    gs = fig.add_gridspec(2, 2, height_ratios=[5, 1])
    ax_graph = fig.add_subplot(gs[0, 0])
    ax_order = fig.add_subplot(gs[0, 1])
    ax_legend = fig.add_subplot(gs[1, :])

    fig.suptitle(
        "Welsh-Powell graph colouring heuristic",
        fontsize=17,
        fontweight="bold",
    )

    msg = [None]

    def update(i: int) -> None:
        step = steps[i]

        ax_graph.clear()
        ax_order.clear()
        ax_legend.clear()

        draw_graph(ax_graph, step)
        draw_order_panel(ax_order, step)
        draw_legend(ax_legend)

        if msg[0] is not None:
            msg[0].remove()

        msg[0] = fig.text(
            0.5,
            0.01,
            f"{step.title}: {step.note}",
            ha="center",
            fontsize=13,
            bbox=dict(boxstyle="round,pad=0.35", facecolor="white", edgecolor="black"),
        )

    interval_ms = 1000.0 / max(args.fps, 0.1)
    anim = FuncAnimation(fig, update, frames=len(steps), interval=interval_ms, repeat=True)
    anim.save(output, writer=PillowWriter(fps=max(args.fps, 0.1)))
    plt.close(fig)

    print(f"[ok] wrote {output}")


if __name__ == "__main__":
    main()
