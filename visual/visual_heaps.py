#!/usr/bin/env python3
"""
Create a GIF comparing a binary heap and a Fibonacci heap.

Focus:
    - Binary heap keeps a compact array-backed tree after every operation.
    - Fibonacci heap inserts lazily by adding nodes to the root list.
    - Fibonacci heap does most restructuring during extract-min, when it consolidates roots.

Operations:
    insert 7
    insert 3
    insert 9
    extract-min
    insert 2
    insert 5

Output:
    visual/binary_heap_vs_fibonacci_heap.gif

Usage:
    python3 visual_heaps.py
    python3 visual_heaps.py --fps 0.8
"""

from __future__ import annotations

import argparse
import heapq
import textwrap
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter


@dataclass
class HeapStep:
    title: str
    binary_array: list[int]
    fib_roots: list[int]
    fib_children: dict[int, list[int]]
    highlight_binary: int | None
    highlight_fib: int | None
    fib_action: str
    note: str


def snapshot(
        title: str,
        binary: list[int],
        roots: list[int],
        children: dict[int, list[int]],
        note: str,
        highlight_binary: int | None = None,
        highlight_fib: int | None = None,
        fib_action: str = "",
) -> HeapStep:
    return HeapStep(
        title=title,
        binary_array=list(binary),
        fib_roots=list(roots),
        fib_children={k: list(v) for k, v in children.items()},
        highlight_binary=highlight_binary,
        highlight_fib=highlight_fib,
        fib_action=fib_action,
        note=note,
    )


def build_steps() -> list[HeapStep]:
    binary: list[int] = []

    # This is not a full Fibonacci heap implementation.
    # It is a didactic state model that shows the key structural idea:
    # insert = add to root list; extract-min = consolidate root list.
    roots: list[int] = []
    children: dict[int, list[int]] = {}

    steps: list[HeapStep] = []

    steps.append(
        snapshot(
            "Initial state",
            binary,
            roots,
            children,
            "Both heaps start empty.",
        )
    )

    # insert 7
    heapq.heappush(binary, 7)
    roots.append(7)
    children.setdefault(7, [])
    steps.append(
        snapshot(
            "insert(7)",
            binary,
            roots,
            children,
            "Binary heap inserts 7 into the array. Fibonacci heap only adds 7 to the root list.",
            highlight_binary=7,
            highlight_fib=7,
            fib_action="lazy insert: add new root",
        )
    )

    # insert 3
    before = list(binary)
    heapq.heappush(binary, 3)
    roots.append(3)
    children.setdefault(3, [])
    steps.append(
        snapshot(
            "insert(3)",
            binary,
            roots,
            children,
            "Binary heap inserts 3 and bubbles it up to preserve heap order. Fibonacci heap still only appends a root.",
            highlight_binary=3,
            highlight_fib=3,
            fib_action="lazy insert: add new root",
        )
    )

    # insert 9
    heapq.heappush(binary, 9)
    roots.append(9)
    children.setdefault(9, [])
    steps.append(
        snapshot(
            "insert(9)",
            binary,
            roots,
            children,
            "Binary heap keeps the array tree ordered. Fibonacci heap now has three separate roots.",
            highlight_binary=9,
            highlight_fib=9,
            fib_action="lazy insert: add new root",
        )
    )

    # extract-min, phase 1: identify/remove min
    min_value = min(binary)
    steps.append(
        snapshot(
            "extract_min(): find minimum",
            binary,
            roots,
            children,
            "Both heaps remove the minimum value. In the Fibonacci heap, the minimum is one root in the root list.",
            highlight_binary=min_value,
            highlight_fib=min_value,
            fib_action="min root selected",
        )
    )

    # Binary heap after extract
    removed_binary = heapq.heappop(binary)

    # Fibonacci remove min root
    roots.remove(min_value)

    steps.append(
        snapshot(
            "extract_min(): remove minimum",
            binary,
            roots,
            children,
            "Binary heap moves the last array item and restores heap order. Fibonacci heap removes the min root.",
            highlight_binary=None,
            highlight_fib=None,
            fib_action="remove min root",
        )
    )

    # Fibonacci consolidation phase:
    # roots are [7, 9]. They have same degree 0, so link the larger under the smaller.
    if 7 in roots and 9 in roots:
        steps.append(
            snapshot(
                "extract_min(): roots need consolidation",
                binary,
                roots,
                children,
                "After removing the min, the Fibonacci heap has roots with the same degree. It consolidates them.",
                highlight_fib=7,
                fib_action="same degree roots found",
            )
        )

        roots.remove(9)
        children.setdefault(7, []).append(9)

        steps.append(
            snapshot(
                "extract_min(): link trees",
                binary,
                roots,
                children,
                "The larger root 9 is linked under 7. The root list becomes smaller, but the structure is pointer-heavy.",
                highlight_fib=7,
                fib_action="link 9 under 7",
            )
        )

    # insert 2
    heapq.heappush(binary, 2)
    roots.append(2)
    children.setdefault(2, [])
    steps.append(
        snapshot(
            "insert(2)",
            binary,
            roots,
            children,
            "After extract-min, Fibonacci heap returns to cheap insertion: 2 is simply added to the root list.",
            highlight_binary=2,
            highlight_fib=2,
            fib_action="lazy insert: add new root",
        )
    )

    # insert 5
    heapq.heappush(binary, 5)
    roots.append(5)
    children.setdefault(5, [])
    steps.append(
        snapshot(
            "insert(5)",
            binary,
            roots,
            children,
            "Again, binary heap may adjust its array. Fibonacci heap delays restructuring until a future extract-min.",
            highlight_binary=5,
            highlight_fib=5,
            fib_action="lazy insert: add new root",
        )
    )

    return steps


def binary_tree_positions(n: int) -> dict[int, tuple[float, float]]:
    pos = {}

    for i in range(n):
        level = (i + 1).bit_length() - 1
        start = 2 ** level - 1
        idx = i - start
        width = 2 ** level

        x = (idx + 1) / (width + 1)
        y = 0.92 - level * 0.24
        pos[i] = (x, y)

    return pos


def draw_binary_heap(ax, arr: list[int], highlight: int | None) -> None:
    ax.set_title("Binary heap: compact array-backed tree", fontsize=16, fontweight="bold")
    ax.axis("off")
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1.08)

    if not arr:
        ax.text(0.5, 0.55, "empty", ha="center", va="center", fontsize=18)
        return

    pos = binary_tree_positions(len(arr))

    for i in range(len(arr)):
        for child in [2 * i + 1, 2 * i + 2]:
            if child < len(arr):
                x1, y1 = pos[i]
                x2, y2 = pos[child]
                ax.plot([x1, x2], [y1, y2], color="#777777", linewidth=2)

    for i, value in enumerate(arr):
        x, y = pos[i]

        color = "#4da3ff" if value == highlight else "#99ccff"
        lw = 3 if value == highlight else 1.8

        ax.scatter([x], [y], s=1400, color=color, edgecolor="black", linewidth=lw, zorder=3)
        ax.text(x, y, str(value), ha="center", va="center", fontsize=17, fontweight="bold", zorder=4)

    array_text = "array: [" + ", ".join(str(x) for x in arr) + "]"

    ax.text(
        0.5,
        0.05,
        array_text,
        ha="center",
        va="bottom",
        fontsize=14,
        bbox=dict(boxstyle="round,pad=0.45", facecolor="white", edgecolor="black"),
    )


def draw_fibonacci_heap(
        ax,
        roots: list[int],
        children: dict[int, list[int]],
        highlight: int | None,
        action: str,
) -> None:
    ax.set_title("Fibonacci heap: lazy root list + consolidation", fontsize=16, fontweight="bold")
    ax.axis("off")
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1.08)

    if not roots:
        ax.text(0.5, 0.55, "empty", ha="center", va="center", fontsize=18)
        return

    root_y = 0.78
    child_y = 0.49

    xs = [(i + 1) / (len(roots) + 1) for i in range(len(roots))]

    # Root list links.
    if len(roots) > 1:
        for i in range(len(roots) - 1):
            ax.plot(
                [xs[i], xs[i + 1]],
                [root_y, root_y],
                color="#555555",
                linewidth=2.5,
                linestyle="--",
                zorder=1,
            )

    min_root = min(roots)

    for x, value in zip(xs, roots):
        is_min = value == min_root
        is_highlight = value == highlight

        if is_highlight:
            color = "#4da3ff"
            lw = 3
        elif is_min:
            color = "#ffd966"
            lw = 2.4
        else:
            color = "#99dd99"
            lw = 1.8

        ax.scatter([x], [root_y], s=1400, color=color, edgecolor="black", linewidth=lw, zorder=3)
        ax.text(x, root_y, str(value), ha="center", va="center", fontsize=17, fontweight="bold", zorder=4)

        if is_min:
            ax.text(x, root_y + 0.13, "min", ha="center", fontsize=12, fontweight="bold")

        child_values = children.get(value, [])

        for j, child in enumerate(child_values):
            offset = (j - (len(child_values) - 1) / 2) * 0.18
            cx = x + offset
            cy = child_y

            ax.plot([x, cx], [root_y - 0.055, cy + 0.055], color="#777777", linewidth=2)
            ax.scatter([cx], [cy], s=1200, color="#eeeeee", edgecolor="black", linewidth=1.8, zorder=3)
            ax.text(cx, cy, str(child), ha="center", va="center", fontsize=16, fontweight="bold", zorder=4)

    root_text = "root list: " + "  ↔  ".join(str(r) for r in roots)

    ax.text(
        0.5,
        0.05,
        root_text,
        ha="center",
        va="bottom",
        fontsize=14,
        bbox=dict(boxstyle="round,pad=0.45", facecolor="white", edgecolor="black"),
    )

    if action:
        ax.text(
            0.5,
            0.18,
            action,
            ha="center",
            va="bottom",
            fontsize=13,
            fontweight="bold",
            color="#333333",
            bbox=dict(boxstyle="round,pad=0.35", facecolor="#f7f7f7", edgecolor="#999999"),
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="visual/binary_heap_vs_fibonacci_heap.gif")
    parser.add_argument("--fps", type=float, default=0.8)
    args = parser.parse_args()

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)

    steps = build_steps()

    fig, axes = plt.subplots(1, 2, figsize=(15, 7.5), facecolor="white")

    fig.suptitle(
        "Binary heap vs Fibonacci heap",
        fontsize=18,
        fontweight="bold",
        y=0.98,
    )

    fig.subplots_adjust(top=0.86, bottom=0.22, wspace=0.16)

    msg = [None]

    def update(i: int) -> None:
        step = steps[i]

        for ax in axes:
            ax.clear()

        draw_binary_heap(axes[0], step.binary_array, step.highlight_binary)
        draw_fibonacci_heap(axes[1], step.fib_roots, step.fib_children, step.highlight_fib, step.fib_action)

        if msg[0] is not None:
            msg[0].remove()

        wrapped = textwrap.fill(f"{step.title}: {step.note}", width=105)

        msg[0] = fig.text(
            0.5,
            0.055,
            wrapped,
            ha="center",
            fontsize=14,
            linespacing=1.25,
            bbox=dict(boxstyle="round,pad=0.45", facecolor="white", edgecolor="black"),
        )

    interval_ms = 1000.0 / max(args.fps, 0.1)

    anim = FuncAnimation(
        fig,
        update,
        frames=len(steps),
        interval=interval_ms,
        repeat=True,
    )

    anim.save(output, writer=PillowWriter(fps=max(args.fps, 0.1)))
    plt.close(fig)

    print(f"[ok] wrote {output}")


if __name__ == "__main__":
    main()