#!/usr/bin/env python3
"""
Run all visual animation scripts.

Usage:
    python3 run_all_visuals.py
    python3 run_all_visuals.py --fps 0.7

Outputs:
    visual/dijkstra_vs_astar.gif
    visual/quadtree_pruning.gif
    visual/coloring_bruteforce_vs_welsh_powell.gif
    visual/binary_heap_vs_fibonacci_heap.gif
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

SCRIPTS = [
    "visual_dijkstra_astar.py",
    "visual_quadtree.py",
    "visual_coloring.py",
    "visual_heaps.py",
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fps", type=float, default=0.8, help="Frames per second passed to each visual script.")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent

    for script in SCRIPTS:
        path = here / script
        if not path.exists():
            raise FileNotFoundError(f"Missing script: {path}")
        print(f"[run] {script}")
        subprocess.run([sys.executable, str(path), "--fps", str(args.fps)], check=True)

    print("\nAll visuals generated in ./visual/")


if __name__ == "__main__":
    main()
