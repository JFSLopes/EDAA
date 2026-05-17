#include "../header/Benchmark.h"
#include "../header/FibonacciHeap.h"
#include "../header/MutablePriorityQueue.h"
#include "../header/BruteForceQueue.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <numeric>
#include <algorithm>
#include <filesystem>
#include <set>
#include <cfloat>
#include <thread>
#include <atomic>
#include <future>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / timing / recording
// ─────────────────────────────────────────────────────────────────────────────

Benchmark::Benchmark(BenchmarkConfig cfg) : cfg(std::move(cfg)) {}

double Benchmark::timed(const std::function<void()>& fn) const {
    auto t0 = std::chrono::high_resolution_clock::now();
    fn();
    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

void Benchmark::record(BenchmarkRow row) { rows.push_back(row); }

// ─────────────────────────────────────────────────────────────────────────────
// CSV output
// ─────────────────────────────────────────────────────────────────────────────

void Benchmark::writeCSV(const std::string& filename,
                         const std::vector<BenchmarkRow>& subset) const {
    std::filesystem::create_directories(cfg.outputDir);
    std::string path = cfg.outputDir + "/" + filename;
    std::ofstream out(path);
    if (!out.is_open()) { std::cerr << "Cannot write " << path << "\n"; return; }
    out << "benchmark,variant,n,param_secondary,elapsed_s,quality,quality_label,dnf,notes\n";
    for (const auto& r : subset) {
        out << r.benchmark    << ","
            << r.variant      << ","
            << r.paramN       << ","
            << r.paramP       << ","
            << std::fixed << std::setprecision(6) << r.elapsedS << ","
            << r.quality      << ","
            << r.qualityLabel << ","
            << (r.dnf ? 1 : 0) << ","
            << r.notes        << "\n";
    }
    std::cout << "Written -> " << path << "  (" << subset.size() << " rows)\n";
}

void Benchmark::writeAllCSVs() const {
    auto filter = [&](const std::string& name) {
        std::vector<BenchmarkRow> out;
        for (auto& r : rows) if (r.benchmark == name) out.push_back(r);
        return out;
    };
    writeCSV("dijkstra_astar.csv",  filter("dijkstra_astar"));
    writeCSV("priority_queues.csv", filter("priority_queues"));
    writeCSV("coloring.csv",        filter("coloring"));
    writeCSV("quadtree.csv",        filter("quadtree"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Random graph builder
// ─────────────────────────────────────────────────────────────────────────────

Multigraph Benchmark::buildRandomGraph(int V, int avgDeg, int seed) const {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> cx(cfg.coordMinX, cfg.coordMaxX);
    std::uniform_real_distribution<double> cy(cfg.coordMinY, cfg.coordMaxY);
    std::uniform_int_distribution<int>     modeD(0, 2);

    static const double SPEEDS[] = { 1.4, 6.9, 11.1 };

    Multigraph g;
    std::vector<std::pair<double,double>> coords(V);
    for (int i = 0; i < V; i++) {
        coords[i] = { cx(rng), cy(rng) };
        g.addVertex(coords[i].first, coords[i].second, "v" + std::to_string(i));
    }

    // Spanning tree for connectivity
    for (int i = 1; i < V; i++) {
        int j = std::uniform_int_distribution<int>(0, i - 1)(rng);
        Mode m = static_cast<Mode>(modeD(rng));
        double dx = coords[i].first  - coords[j].first;
        double dy = coords[i].second - coords[j].second;
        double dist = std::sqrt(dx*dx + dy*dy);
        g.addEdge(i, j, dist / SPEEDS[static_cast<int>(m)], m);
    }

    // Extra random edges up to target average degree
    int target = std::max(0, V * avgDeg / 2 - (V - 1));
    std::uniform_int_distribution<int> vtxD(0, V - 1);
    std::set<std::pair<int,int>> seen;
    for (int k = 0; k < target * 10 && (int)seen.size() < target; k++) {
        int a = vtxD(rng), b = vtxD(rng);
        if (a == b) continue;
        if (a > b) std::swap(a, b);
        if (seen.count({a, b})) continue;
        seen.insert({a, b});
        Mode m = static_cast<Mode>(modeD(rng));
        double dx = coords[a].first  - coords[b].first;
        double dy = coords[a].second - coords[b].second;
        double dist = std::sqrt(dx*dx + dy*dy);
        g.addEdge(a, b, dist / SPEEDS[static_cast<int>(m)], m);
    }
    return g;
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 1 — Dijkstra vs A*
//   • Sweep: n in [100k..1M step 100k]  ×  avgDeg in [10..50 step 10]
//   • PQs: FibonacciHeap, MutablePriorityQueue only
//   • 3 random (src, dst) pairs per combination — average elapsed
// ─────────────────────────────────────────────────────────────────────────────

void Benchmark::runDijkstraVsAstar() {
    std::cout << "\n== Dijkstra vs A* ==\n";

    struct Variant {
        std::string name;
        PriorityQueueSelected pqs;
        bool isStar; // true -> A*, false -> Dijkstra
    };
    const std::vector<Variant> variants = {
            { "Dijkstra_FibHeap",  FIBONACCI_HEAP,         false },
            { "Dijkstra_BinHeap",  MUTABLE_PRIORITY_QUEUE, false },
            { "AStar_FibHeap",     FIBONACCI_HEAP,         true  },
            { "AStar_BinHeap",     MUTABLE_PRIORITY_QUEUE, true  },
    };

    for (int avgDeg : cfg.dijkstraAvgDegrees) {
        std::cout << "  avgDeg=" << avgDeg << "\n";

        // Per-variant DNF flag — reset for each degree sweep
        std::vector<bool> dnfReached(variants.size(), false);

        for (int V : cfg.dijkstraVertexCounts) {

            // Check if all variants already DNF'd
            bool allDnf = true;
            for (bool d : dnfReached) if (!d) { allDnf = false; break; }
            if (allDnf) {
                std::cout << "    All variants DNF, skipping V>=" << V << "\n";
                break;
            }

            std::cout << "    Building V=" << V << " deg=" << avgDeg << "...\n";
            Multigraph g = buildRandomGraph(V, avgDeg,
                                            cfg.dijkstraRngSeed + avgDeg);

            // Generate cfg.dijkstraSrcDstPairs random (src, dst) pairs
            std::mt19937 rng(cfg.dijkstraRngSeed + V + avgDeg);
            std::uniform_int_distribution<int> vtxD(0, V - 1);
            std::vector<std::pair<int,int>> pairs;
            while ((int)pairs.size() < cfg.dijkstraSrcDstPairs) {
                int s = vtxD(rng), d = vtxD(rng);
                if (s != d) pairs.push_back({s, d});
            }

            for (int vi = 0; vi < (int)variants.size(); vi++) {
                const auto& var = variants[vi];
                if (dnfReached[vi]) continue;

                double totalElapsed = 0.0;
                double totalCost    = 0.0;
                int    validCosts   = 0;
                bool   dnf          = false;

                for (auto& [si, di] : pairs) {
                    auto src  = g.getVertex(si);
                    auto dest = g.getVertex(di);

                    double elapsed = 0.0;
                    if (var.isStar) {
                        elapsed = timed([&]{ g.astar_aux(src, dest, var.pqs); });
                    } else {
                        elapsed = timed([&]{ g.dijkstra_aux(src, dest, var.pqs); });
                    }
                    double cost = dest->getDist();
                    totalElapsed += elapsed;
                    if (cost < DBL_MAX) { totalCost += cost; validCosts++; }

                    if (elapsed > cfg.maxSecondsPerRun) { dnf = true; break; }
                }

                double avgElapsed = totalElapsed / (double)cfg.dijkstraSrcDstPairs;
                double avgCost    = validCosts > 0 ? totalCost / validCosts : -1.0;

                BenchmarkRow row;
                row.benchmark    = "dijkstra_astar";
                row.variant      = var.name;
                row.paramN       = V;
                row.paramP       = avgDeg;
                row.elapsedS     = avgElapsed;
                row.quality      = avgCost;
                row.qualityLabel = "avg_path_cost_s";
                row.dnf          = dnf;
                row.notes        = "pairs=" + std::to_string(cfg.dijkstraSrcDstPairs);
                record(row);

                if (dnf) dnfReached[vi] = true;
            }
        }
    }
    std::cout << "  Done.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 2 — Priority Queue comparison: BruteForce vs FibonacciHeap
//   • Sweep: n in [10k..50k step 10k]  ×  avgDeg in [10, 20, 30]
//   • 2 runs per (n, deg, pq) — average elapsed
// ─────────────────────────────────────────────────────────────────────────────

void Benchmark::runPriorityQueues() {
    std::cout << "\n== Priority Queue Comparison (BruteForce vs FibHeap) ==\n";

    struct Variant { std::string name; PriorityQueueSelected pqs; };
    const std::vector<Variant> variants = {
            { "FibonacciHeap", FIBONACCI_HEAP  },
            { "BruteForce",    BRUTE_FORCE     },
    };

    for (int avgDeg : cfg.pqAvgDegrees) {
        std::cout << "  avgDeg=" << avgDeg << "\n";

        std::vector<bool> dnfReached(variants.size(), false);

        for (int V : cfg.pqVertexCounts) {
            std::cout << "    Building V=" << V << " deg=" << avgDeg << "...\n";
            Multigraph g = buildRandomGraph(V, avgDeg,
                                            cfg.pqRngSeed + avgDeg);

            std::mt19937 rng(cfg.pqRngSeed + V + avgDeg);
            std::uniform_int_distribution<int> vtxD(0, V - 1);

            for (int vi = 0; vi < (int)variants.size(); vi++) {
                const auto& var = variants[vi];
                if (dnfReached[vi]) continue;

                double totalElapsed = 0.0;
                bool   dnf          = false;

                for (int run = 0; run < cfg.pqRuns; run++) {
                    int si = vtxD(rng), di = vtxD(rng);
                    while (di == si) di = vtxD(rng);
                    auto src  = g.getVertex(si);
                    auto dest = g.getVertex(di);

                    double elapsed = timed([&]{
                        g.dijkstra_aux(src, dest, var.pqs);
                    });
                    totalElapsed += elapsed;
                    if (elapsed > cfg.maxSecondsPerRun) { dnf = true; break; }
                }

                double avgElapsed = totalElapsed / (double)cfg.pqRuns;

                BenchmarkRow row;
                row.benchmark    = "priority_queues";
                row.variant      = var.name;
                row.paramN       = V;
                row.paramP       = avgDeg;
                row.elapsedS     = avgElapsed;
                row.quality      = 0;
                row.qualityLabel = "none";
                row.dnf          = dnf;
                row.notes        = "runs=" + std::to_string(cfg.pqRuns);
                record(row);

                if (dnf) dnfReached[vi] = true;
            }
        }
    }
    std::cout << "  Done.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Coloring helpers
// ─────────────────────────────────────────────────────────────────────────────

Coloring Benchmark::buildColoringInstance(int N, double radius, int seed) const {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> rx(cfg.coordMinX, cfg.coordMaxX);
    std::uniform_real_distribution<double> ry(cfg.coordMinY, cfg.coordMaxY);

    std::ostringstream jss;
    jss << "{ \"interference_radius\": " << radius << ",\n  \"antennas\": [\n";
    for (int i = 0; i < N; i++) {
        jss << "    {\"id\":\"ap_" << i << "\","
            << "\"x\":" << std::fixed << std::setprecision(2) << rx(rng) << ","
            << "\"y\":" << ry(rng) << "}";
        if (i + 1 < N) jss << ",";
        jss << "\n";
    }
    jss << "  ]\n}";

    std::filesystem::create_directories(cfg.outputDir);
    std::string tmpPath = cfg.outputDir + "/_bench_tmp_" + std::to_string(seed) + ".json";
    { std::ofstream tf(tmpPath); tf << jss.str(); }

    Coloring col;
    col.loadFromJson(tmpPath);
    std::filesystem::remove(tmpPath);
    return col;
}

// Runs BruteForce with a hard wall-clock timeout using a detached future.
// The BF call is NOT interrupted mid-run (C++ can't do that cleanly), but we
// record elapsed and mark DNF if it exceeded the budget.
Benchmark::BFResult Benchmark::runBruteForceWithTimeout(const Coloring& col) const {
    auto t0 = std::chrono::high_resolution_clock::now();

    // Run brute force in a future so we can check elapsed after the fact.
    // We still wait for it to finish — the DNF flag is purely informational.
    auto fut = std::async(std::launch::async, [&]() -> Coloring::Solution {
        return col.solveBruteForce();
    });

    // Poll until done or timeout
    Coloring::Solution sol;
    bool timedOut = false;
    auto deadline = t0 + std::chrono::duration<double>(cfg.coloringBruteForceTimeout);

    while (true) {
        auto status = fut.wait_until(deadline);
        if (status == std::future_status::ready) {
            sol = fut.get();
            break;
        }
        // Deadline passed — still wait for completion but mark DNF
        timedOut = true;
        sol = fut.get();   // must collect to avoid std::future destructor block
        break;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    return { elapsed, sol.colorsUsed, timedOut || elapsed > cfg.coloringBruteForceTimeout, sol.feasible };
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 3 — Graph Coloring: BruteForce vs Welsh-Powell
//
//   Part A — Random instances:
//     Sweep antenna count × interference radius.
//     BF always runs but is marked DNF if it exceeds coloringBruteForceTimeout.
//     Even DNF rows record the actual elapsed time so plots show the explosion.
//
//   Part B — Hard instances:
//     • "Dense" graph: antennas packed into a tiny area so almost every pair
//       conflicts → chromatic number ≈ N (worst case for BF).
//     • "Petersen-like" graph: antennas arranged to approximate a
//       triangle-free 3-chromatic graph. BF must explore more branches since
//       no greedy early-cut is possible.
//     • "Sparse" graph: low-radius, few conflicts → easy for both.
//     Welsh-Powell should find optimal or near-optimal on these.
// ─────────────────────────────────────────────────────────────────────────────

void Benchmark::runColoring() {
    std::cout << "\n== Graph Coloring ==\n";

    // ── Part A: random instances ──────────────────────────────────────────────
    std::cout << "  [A] Random instances\n";

    for (double radius : cfg.coloringRadii) {
        for (int N : cfg.coloringAntennaCounts) {
            int seed = cfg.coloringRngSeed + N + (int)radius;
            Coloring col = buildColoringInstance(N, radius, seed);

            size_t edges   = col.getConflictEdgeCount();
            double density = N > 1
                             ? 2.0 * (double)edges / ((double)N * (N - 1)) : 0.0;
            std::string note = "edges=" + std::to_string(edges)
                               + " density=" + std::to_string(density).substr(0, 5)
                               + " r=" + std::to_string((int)radius);

            // Welsh-Powell
            {
                Coloring::Solution sol;
                double elapsed = timed([&]{ sol = col.solveWelshPowell(); });
                BenchmarkRow row;
                row.benchmark    = "coloring";
                row.variant      = "WelshPowell";
                row.paramN       = N;
                row.paramP       = radius;
                row.elapsedS     = elapsed;
                row.quality      = sol.colorsUsed;
                row.qualityLabel = "colors_used";
                row.notes        = note;
                record(row);
            }

            // BruteForce — always run, record time even on DNF
            {
                auto bfr = runBruteForceWithTimeout(col);
                BenchmarkRow row;
                row.benchmark    = "coloring";
                row.variant      = "BruteForce";
                row.paramN       = N;
                row.paramP       = radius;
                row.elapsedS     = bfr.elapsed;
                row.quality      = bfr.colorsUsed;
                row.qualityLabel = "colors_used";
                row.dnf          = bfr.dnf;
                row.notes        = note + (bfr.dnf ? " DNF" : "");
                record(row);
            }
        }
    }

    // ── Part B: hard instances ────────────────────────────────────────────────
    std::cout << "  [B] Hard instances\n";

    for (int N : cfg.hardColoringAntennaCounts) {

        // --- Dense graph: pack all antennas in a 100m × 100m cell -----------
        // Every pair within 2000m → complete graph → χ = N → BF explodes.
        {
            const double TINY = 100.0;
            std::mt19937 rng(cfg.coloringRngSeed + N * 1000);
            std::uniform_real_distribution<double>
                    rx(cfg.coordMinX, cfg.coordMinX + TINY),
                    ry(cfg.coordMinY, cfg.coordMinY + TINY);

            std::ostringstream jss;
            jss << "{ \"interference_radius\": 2000.0,\n  \"antennas\": [\n";
            for (int i = 0; i < N; i++) {
                jss << "    {\"id\":\"ap_" << i << "\","
                    << "\"x\":" << std::fixed << std::setprecision(2) << rx(rng) << ","
                    << "\"y\":" << ry(rng) << "}";
                if (i + 1 < N) jss << ",";
                jss << "\n";
            }
            jss << "  ]\n}";

            std::string tmpPath = cfg.outputDir + "/_bench_tmp_dense.json";
            { std::ofstream tf(tmpPath); tf << jss.str(); }
            Coloring col;
            col.loadFromJson(tmpPath);
            std::filesystem::remove(tmpPath);

            size_t edges = col.getConflictEdgeCount();
            std::string note = "hard_dense edges=" + std::to_string(edges);

            // WP
            {
                Coloring::Solution sol;
                double elapsed = timed([&]{ sol = col.solveWelshPowell(); });
                BenchmarkRow row;
                row.benchmark = "coloring"; row.variant = "WelshPowell_Hard_Dense";
                row.paramN = N; row.paramP = 2000.0;
                row.elapsedS = elapsed; row.quality = sol.colorsUsed;
                row.qualityLabel = "colors_used"; row.notes = note;
                record(row);
            }
            // BF
            {
                auto bfr = runBruteForceWithTimeout(col);
                BenchmarkRow row;
                row.benchmark = "coloring"; row.variant = "BruteForce_Hard_Dense";
                row.paramN = N; row.paramP = 2000.0;
                row.elapsedS = bfr.elapsed; row.quality = bfr.colorsUsed;
                row.qualityLabel = "colors_used"; row.dnf = bfr.dnf;
                row.notes = note + (bfr.dnf ? " DNF" : "");
                record(row);
            }
        }

        // --- Petersen-like graph (triangle-free, χ=3) -----------------------
        // Place N antennas on two concentric rings arranged so that the
        // adjacency approximates a circulant graph with no triangles.
        // χ = 3 is provably necessary but WP may need 3 or 4 colours.
        // BF must explore because no clique of size 3 provides an early lower
        // bound — it has to try all 2-colourings before concluding 3 are needed.
        {
            // Outer ring: N/2 nodes; inner ring: N/2 nodes.
            // Interference radius is set so each outer node covers its two
            // nearest inner nodes and vice versa but outer nodes do NOT cover
            // each other — triangle-free by construction.
            int outer = N / 2;
            int inner = N - outer;
            double cx  = (cfg.coordMinX + cfg.coordMaxX) / 2.0;
            double cy  = (cfg.coordMinY + cfg.coordMaxY) / 2.0;
            double Ro  = 1500.0;   // outer ring radius (metres)
            double Ri  = 750.0;    // inner ring radius
            // Interference radius chosen so outer-inner edges exist but
            // outer-outer edges do not (outer nodes are 2*Ro*sin(π/outer) apart).
            double outerSpacing = 2.0 * Ro * std::sin(M_PI / std::max(outer, 1));
            double ifRadius     = outerSpacing * 0.9; // just under neighbour gap

            std::ostringstream jss;
            jss << "{ \"interference_radius\": " << std::fixed << std::setprecision(1)
                << ifRadius << ",\n  \"antennas\": [\n";

            auto writeNode = [&](int idx, double x, double y, bool last) {
                jss << "    {\"id\":\"ap_" << idx << "\","
                    << "\"x\":" << std::fixed << std::setprecision(2) << x << ","
                    << "\"y\":" << y << "}";
                if (!last) jss << ",";
                jss << "\n";
            };

            int idx = 0;
            for (int i = 0; i < outer; i++) {
                double angle = 2.0 * M_PI * i / outer;
                writeNode(idx++, cx + Ro * std::cos(angle), cy + Ro * std::sin(angle),
                          idx == N);
            }
            for (int i = 0; i < inner; i++) {
                double angle = 2.0 * M_PI * i / inner + M_PI / inner; // rotated
                writeNode(idx++, cx + Ri * std::cos(angle), cy + Ri * std::sin(angle),
                          idx == N);
            }
            jss << "  ]\n}";

            std::string tmpPath = cfg.outputDir + "/_bench_tmp_petersen.json";
            { std::ofstream tf(tmpPath); tf << jss.str(); }
            Coloring col;
            col.loadFromJson(tmpPath);
            std::filesystem::remove(tmpPath);

            size_t edges = col.getConflictEdgeCount();
            std::string note = "hard_petersen edges=" + std::to_string(edges)
                               + " ifRadius=" + std::to_string((int)ifRadius);

            // WP
            {
                Coloring::Solution sol;
                double elapsed = timed([&]{ sol = col.solveWelshPowell(); });
                BenchmarkRow row;
                row.benchmark = "coloring"; row.variant = "WelshPowell_Hard_Petersen";
                row.paramN = N; row.paramP = ifRadius;
                row.elapsedS = elapsed; row.quality = sol.colorsUsed;
                row.qualityLabel = "colors_used"; row.notes = note;
                record(row);
            }
            // BF
            {
                auto bfr = runBruteForceWithTimeout(col);
                BenchmarkRow row;
                row.benchmark = "coloring"; row.variant = "BruteForce_Hard_Petersen";
                row.paramN = N; row.paramP = ifRadius;
                row.elapsedS = bfr.elapsed; row.quality = bfr.colorsUsed;
                row.qualityLabel = "colors_used"; row.dnf = bfr.dnf;
                row.notes = note + (bfr.dnf ? " DNF" : "");
                record(row);
            }
        }
    }

    std::cout << "  Done.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 4 — Quadtree vs Brute Force: interference graph construction
//
//   For each (N, radius) pair, build the full interference graph:
//     BF:       O(n²) — check all pairs
//     Quadtree: O(n log n) — for each node, rangeSearch(radius)
//   We record total time to build the edge list (what the Coloring solver
//   actually does), which is a much more realistic workload than single
//   nearest-neighbour queries.
// ─────────────────────────────────────────────────────────────────────────────

void Benchmark::runQuadtree() {
    std::cout << "\n== Quadtree vs BruteForce — Interference Graph Construction ==\n";

    for (double radius : cfg.quadtreeRadii) {
        std::cout << "  radius=" << (int)radius << "m\n";

        for (int N : cfg.quadtreeNodeCounts) {
            // Generate N random antennas
            std::mt19937 rng(cfg.quadtreeRngSeed + N + (int)radius);
            std::uniform_real_distribution<double>
                    rx(cfg.coordMinX, cfg.coordMaxX),
                    ry(cfg.coordMinY, cfg.coordMaxY);

            struct Point { double x, y; };
            std::vector<Point> pts(N);
            std::vector<std::shared_ptr<Vertex>> verts;
            verts.reserve(N);
            for (int i = 0; i < N; i++) {
                pts[i] = { rx(rng), ry(rng) };
                verts.push_back(std::make_shared<Vertex>(
                        i, pts[i].x, pts[i].y, "ap_" + std::to_string(i)));
            }

            // ── Quadtree: build + range query for every node ──────────────
            long edgesQt = 0;
            double buildTime = 0.0;
            double elapsed_qt = timed([&]{
                std::unique_ptr<Quadtree> qt;
                buildTime = timed([&]{ qt = std::make_unique<Quadtree>(verts); });
                for (int i = 0; i < N; i++) {
                    auto neighbours = qt->rangeSearch(pts[i].x, pts[i].y, radius);
                    for (auto* nb : neighbours)
                        if ((int)nb->getId() > i) edgesQt++;
                }
            });

            {
                BenchmarkRow row;
                row.benchmark    = "quadtree";
                row.variant      = "Quadtree";
                row.paramN       = N;
                row.paramP       = radius;
                row.elapsedS     = elapsed_qt;
                row.quality      = buildTime;
                row.qualityLabel = "build_time_s";
                row.notes        = "edges_found=" + std::to_string(edgesQt)
                                   + " radius=" + std::to_string((int)radius);
                record(row);
            }

            // ── Brute force: O(n²) pair check ────────────────────────────
            long edgesBf = 0;
            double elapsed_bf = timed([&]{
                double r2 = radius * radius;
                for (int i = 0; i < N; i++) {
                    for (int j = i + 1; j < N; j++) {
                        double dx = pts[i].x - pts[j].x;
                        double dy = pts[i].y - pts[j].y;
                        if (dx*dx + dy*dy <= r2) edgesBf++;
                    }
                }
            });

            {
                BenchmarkRow row;
                row.benchmark    = "quadtree";
                row.variant      = "BruteForce";
                row.paramN       = N;
                row.paramP       = radius;
                row.elapsedS     = elapsed_bf;
                row.quality      = 0;
                row.qualityLabel = "build_time_s";
                row.notes        = "edges_found=" + std::to_string(edgesBf)
                                   + " radius=" + std::to_string((int)radius);
                record(row);
            }

            std::cout << "    N=" << N
                      << "  QT=" << std::fixed << std::setprecision(4) << elapsed_qt << "s"
                      << "  BF=" << elapsed_bf << "s"
                      << "  edges=" << edgesBf << "\n";
        }
    }
    std::cout << "  Done.\n";
}


void Benchmark::runCorrectnessCheck() {
    std::cout << "\n== Correctness Check: FibHeap vs MutablePQ distances ==\n";

    int totalPairs    = 0;
    int mismatchesPQ  = 0;   // FibHeap Dijkstra vs MutablePQ Dijkstra
    int mismatchesStar = 0;  // A* (FibHeap) vs Dijkstra (FibHeap)
    const double EPS  = 1e-6;

    for (int avgDeg : cfg.correctnessAvgDegrees) {
        for (int V : cfg.correctnessVertexCounts) {
            Multigraph g = buildRandomGraph(V, avgDeg,
                                            cfg.correctnessRngSeed + V + avgDeg);

            std::mt19937 rng(cfg.correctnessRngSeed + V + avgDeg);
            std::uniform_int_distribution<int> vtxD(0, V - 1);

            int localMismatchPQ   = 0;
            int localMismatchStar = 0;

            for (int p = 0; p < cfg.correctnessPairsPerGraph; p++) {
                int si = vtxD(rng), di = vtxD(rng);
                while (di == si) di = vtxD(rng);

                auto src  = g.getVertex(si);
                auto dest = g.getVertex(di);

                // Dijkstra with FibHeap
                g.dijkstra_aux(src, dest, FIBONACCI_HEAP);
                double distFib = dest->getDist();

                // Dijkstra with MutablePQ
                g.dijkstra_aux(src, dest, MUTABLE_PRIORITY_QUEUE);
                double distMut = dest->getDist();

                // A* with FibHeap
                g.astar_aux(src, dest, FIBONACCI_HEAP);
                double distAStar = dest->getDist();

                totalPairs++;

                bool bothInf = (distFib >= DBL_MAX / 2) && (distMut >= DBL_MAX / 2);
                if (!bothInf && std::abs(distFib - distMut) > EPS) {
                    localMismatchPQ++;
                    mismatchesPQ++;
                    std::cout << "  [MISMATCH PQ] V=" << V << " deg=" << avgDeg
                              << " src=" << si << " dst=" << di
                              << "  FibHeap=" << distFib
                              << "  MutablePQ=" << distMut << "\n";
                }

                bool astarBothInf = (distFib >= DBL_MAX / 2) && (distAStar >= DBL_MAX / 2);
                if (!astarBothInf && std::abs(distFib - distAStar) > EPS) {
                    localMismatchStar++;
                    mismatchesStar++;
                    std::cout << "  [MISMATCH A*] V=" << V << " deg=" << avgDeg
                              << " src=" << si << " dst=" << di
                              << "  Dijkstra=" << distFib
                              << "  A*=" << distAStar << "\n";
                }
            }

            std::string statusPQ   = localMismatchPQ   == 0 ? "OK" : "FAIL";
            std::string statusStar = localMismatchStar == 0 ? "OK" : "FAIL";
            std::cout << "  V=" << V << " deg=" << avgDeg
                      << "  pairs=" << cfg.correctnessPairsPerGraph
                      << "  Dijkstra(Fib vs Mut)=" << statusPQ
                      << "  A*(Fib) vs Dijkstra(Fib)=" << statusStar << "\n";
        }
    }

    std::cout << "\n  --- Correctness summary ---\n"
              << "  Total pairs checked : " << totalPairs << "\n"
              << "  Dijkstra PQ mismatches : " << mismatchesPQ
              << (mismatchesPQ == 0 ? "  [ALL OK]" : "  [FAILURES DETECTED]") << "\n"
              << "  A* vs Dijkstra mismatches : " << mismatchesStar
              << (mismatchesStar == 0 ? "  [ALL OK]" : "  [FAILURES DETECTED]") << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// runAll
// ─────────────────────────────────────────────────────────────────────────────

void Benchmark::runAll() {
    std::cout << "\n=== EDAA Benchmark Suite ===\n"
              << "  Output: " << cfg.outputDir << "\n\n";

    //runCorrectnessCheck();
    runDijkstraVsAstar();
    //runPriorityQueues();
    //runColoring();
    //runQuadtree();

    writeAllCSVs();
    std::cout << "\nTotal rows: " << rows.size() << "  Done.\n\n";
}