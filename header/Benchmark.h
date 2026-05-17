#ifndef EDAA_BENCHMARK_H
#define EDAA_BENCHMARK_H

#include "Multigraph.h"
#include "Coloring.h"
#include "Quadtree.h"

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <optional>

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

struct BenchmarkConfig {

    // ── Output ────────────────────────────────────────────────────────────────
    std::string outputDir = "../benchmark";

    // ── Global time budget ────────────────────────────────────────────────────
    double maxSecondsPerRun = 60.0;   ///< Hard wall-clock limit per single run

    // ── Dijkstra / A*  ────────────────────────────────────────────────────────
    // Graph sizes: 100 000 to 1 000 000 in steps of 100 000
    //std::vector<int> dijkstraVertexCounts = {
    //        100000, 200000, 300000, 400000, 500000,
    //        600000, 700000, 800000, 900000, 1000000
    //};
    std::vector<int> dijkstraVertexCounts = {
            100000, 200000, 300000, 400000
    };
    // Average degree values: 10, 20, 30, 40, 50
    //std::vector<int> dijkstraAvgDegrees = { 10, 20, 30, 40, 50 };
    std::vector<int> dijkstraAvgDegrees = { 20 };
    int dijkstraSrcDstPairs = 3;   ///< Random (src, dst) pairs per run — results are averaged
    int dijkstraRngSeed     = 42;

    // ── Priority Queue comparison — BruteForce vs FibHeap ────────────────────
    // Graph sizes: 10 000 to 50 000 in steps of 10 000
    std::vector<int> pqVertexCounts  = { 10000, 20000, 30000, 40000, 50000 };
    // Average degree values: 10, 20, 30
    std::vector<int> pqAvgDegrees    = { 10, 20, 30 };
    int pqRuns    = 2;
    int pqRngSeed = 7;

    // ── Graph Coloring ─────────────────────────────────────────────────────────
    // Random antenna instances
    std::vector<int>    coloringAntennaCounts = { 5, 8, 10, 12, 14, 16, 18, 20, 25, 30 };
    std::vector<double> coloringRadii         = { 500.0, 1000.0, 2000.0 };
    // Brute force wall-clock limit (seconds) — run is recorded as DNF if exceeded
    double coloringBruteForceTimeout = 20.0;
    int    coloringRngSeed = 13;

    // ── Hard coloring instances — forces BF to explore large search spaces ────
    // We generate one Petersen-like graph (triangle-free, chromatic number = 3)
    // and one dense near-complete graph per size to stress BF.
    std::vector<int> hardColoringAntennaCounts = { 10, 15, 20 };

    // ── Quadtree vs BruteForce — interference graph construction ─────────────
    // Benchmark: for each node set, build a full interference graph using
    // (a) Quadtree range queries  (b) O(n²) brute-force scan.
    // This is the actual use-case in the coloring solver.
    std::vector<int>    quadtreeNodeCounts = {
            500, 1000, 2000, 5000, 10000, 20000, 50000, 100000
    };
    std::vector<double> quadtreeRadii = { 500.0, 1000.0, 2000.0 };
    int quadtreeRngSeed = 99;

    std::vector<int> correctnessVertexCounts = { 100, 500, 1000, 5000, 10000 };
    std::vector<int> correctnessAvgDegrees   = { 5, 15, 30 };
    int correctnessPairsPerGraph = 20;  ///< (src, dst) pairs checked per graph
    int correctnessRngSeed       = 2024;

    // ── UTM coordinate bounding box (Porto area) ──────────────────────────────
    double coordMinX = 526000.0;
    double coordMaxX = 536000.0;
    double coordMinY = 4554000.0;
    double coordMaxY = 4560000.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// One row of result data — written to CSV
// ─────────────────────────────────────────────────────────────────────────────

struct BenchmarkRow {
    std::string benchmark;       ///< "dijkstra_astar", "priority_queues", "coloring", "quadtree"
    std::string variant;         ///< Algorithm/PQ label
    int         paramN    = 0;   ///< Primary size (vertices / antennas / nodes)
    double      paramP    = 0;   ///< Secondary param (avg degree / radius)
    double      elapsedS  = 0;   ///< Wall-clock seconds (average when multiple runs)
    double      quality   = 0;   ///< Algorithm-specific quality metric
    std::string qualityLabel;    ///< What quality means
    bool        dnf       = false;
    std::string notes;
};

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark runner
// ─────────────────────────────────────────────────────────────────────────────

class Benchmark {
public:
    explicit Benchmark(BenchmarkConfig cfg = {});

    void runAll();

    void runDijkstraVsAstar();
    void runPriorityQueues();
    void runColoring();
    void runQuadtree();
    void runCorrectnessCheck();

private:
    BenchmarkConfig cfg;
    std::vector<BenchmarkRow> rows;

    Multigraph buildRandomGraph(int V, int avgDeg, int seed) const;

    /// Times fn(); returns elapsed seconds.
    double timed(const std::function<void()>& fn) const;

    void record(BenchmarkRow row);
    void writeCSV(const std::string& filename,
                  const std::vector<BenchmarkRow>& subset) const;
    void writeAllCSVs() const;

    /// Write a temporary coloring JSON and return a loaded Coloring object.
    Coloring buildColoringInstance(int N, double radius, int seed) const;

    /// Run BF with a wall-clock timeout; returns elapsed and marks dnf if exceeded.
    struct BFResult { double elapsed; int colorsUsed; bool dnf; bool feasible; };
    BFResult runBruteForceWithTimeout(const Coloring& col) const;
};

#endif // EDAA_BENCHMARK_H