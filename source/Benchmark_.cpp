#include "../header/Benchmark.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <filesystem>
#include <set>
#include <cfloat>
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

double Benchmark::timedAverage(const std::function<void()>& fn, int totalRuns, int warmupRuns) const {
    double total = 0.0;
    int countedRuns = 0;

    for (int run = 0; run < totalRuns; run++) {
        double elapsed = timed(fn);

        if (run >= warmupRuns) {
            total += elapsed;
            countedRuns++;
        }
    }

    return countedRuns > 0 ? total / countedRuns : 0.0;
}

Benchmark::TimedPQResult Benchmark::timedPQAverage(const std::function<void()>& fn, int totalRuns, int warmupRuns) const {
    double totalElapsed = 0.0;
    PriorityQueueStats totalStats;
    int countedRuns = 0;

    for (int run = 0; run < totalRuns; run++) {
        PriorityQueue::resetStats();

        double elapsed = timed(fn);
        PriorityQueueStats stats = PriorityQueue::getStats();

        if (run >= warmupRuns) {
            totalElapsed += elapsed;
            totalStats += stats;
            countedRuns++;
        }
    }

    TimedPQResult result;
    result.elapsed = countedRuns > 0 ? totalElapsed / countedRuns : 0.0;
    result.stats = totalStats / countedRuns;
    return result;
}

void Benchmark::record(BenchmarkRow row) { rows.push_back(row); }

// ─────────────────────────────────────────────────────────────────────────────
// CSV output
// ─────────────────────────────────────────────────────────────────────────────

void Benchmark::writeCSV(const std::string& filename,
                         const std::vector<BenchmarkRow>& subset) const {

    if (subset.empty()) {
        std::cout << "Nothing to write into " << filename << ". Skipping\n";
        return;
    }

    std::filesystem::create_directories(cfg.outputDir);
    std::string path = cfg.outputDir + "/" + filename;
    std::ofstream out(path);
    if (!out.is_open()) { std::cerr << "Cannot write " << path << "\n"; return; }
    out << "benchmark,variant,n,param_secondary,elapsed_s,quality,quality_label,"
           "pq_inserts,pq_deletes,pq_update_keys,dnf,notes\n";
    for (const auto& r : subset) {
        out << r.benchmark    << ","
            << r.variant      << ","
            << r.paramN       << ","
            << r.paramP       << ","
            << std::fixed << std::setprecision(6) << r.elapsedS << ","
            << r.quality      << ","
            << r.qualityLabel << ","
            << r.pqInserts    << ","
            << r.pqDeletes    << ","
            << r.pqUpdateKeys << ","
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
    writeCSV("prim.csv",            filter("prim"));
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
                        elapsed = timedAverage([&]{ g.astar_aux(src, dest, var.pqs); });
                    } else {
                        elapsed = timedAverage([&]{ g.dijkstra_aux(src, dest, var.pqs); });
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
            { "BinaryHeap",    MUTABLE_PRIORITY_QUEUE },
            { "FibonacciHeap", FIBONACCI_HEAP         },
            { "BruteForce",    BRUTE_FORCE            },
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
                PriorityQueueStats totalStats;
                bool dnf = false;

                for (int run = 0; run < cfg.pqRuns; run++) {
                    int si = vtxD(rng), di = vtxD(rng);
                    while (di == si) di = vtxD(rng);
                    auto src  = g.getVertex(si);
                    auto dest = g.getVertex(di);

                    auto result = timedPQAverage([&]{
                        g.dijkstra_aux(src, dest, var.pqs);
                    });

                    totalElapsed += result.elapsed;
                    totalStats += result.stats;

                    if (result.elapsed > cfg.maxSecondsPerRun) {
                        dnf = true;
                        break;
                    }
                }

                double avgElapsed = totalElapsed / (double)cfg.pqRuns;
                PriorityQueueStats avgStats = totalStats / cfg.pqRuns;
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
                double elapsed = timedAverage([&]{ sol = col.solveWelshPowell(); });
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
                double elapsed = timedAverage([&]{ sol = col.solveWelshPowell(); });
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
                double elapsed = timedAverage([&]{ sol = col.solveWelshPowell(); });
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
    std::cout << "\n== Quadtree vs BruteForce — Connectivity Sweep ==\n";

    // We do not need huge graphs to find the degradation point.
    // A fixed N makes the effect of connectivity/radius easier to see.
    const std::vector<int> nodeCounts = { 5000, 10000 };

    const double width  = cfg.coordMaxX - cfg.coordMinX;
    const double height = cfg.coordMaxY - cfg.coordMinY;
    const double base   = std::min(width, height);

    // Connectivity sweep.
    // If your coordinate range is 0..10000, this gives:
    // 50, 100, 200, 300, 500, 750, 1000, 1500, 2000, 3000
    const std::vector<double> radiusFactors = {
            0.005, 0.010, 0.020, 0.030, 0.050,
            0.075, 0.100, 0.150, 0.200, 0.300
    };

    bool globalFoundBreakingPoint = false;

    for (int N : nodeCounts) {
        std::cout << "\n  N=" << N << "\n";

        bool foundBreakingPointForN = false;
        double breakingAvgDegree = -1.0;
        double breakingRadius = -1.0;

        for (double factor : radiusFactors) {
            double radius = base * factor;

            std::mt19937 rng(cfg.quadtreeRngSeed + N + (int)radius);
            std::uniform_real_distribution<double>
                    rx(cfg.coordMinX, cfg.coordMaxX),
                    ry(cfg.coordMinY, cfg.coordMaxY);

            struct Point {
                double x;
                double y;
            };

            std::vector<Point> pts(N);
            std::vector<std::shared_ptr<Vertex>> verts;
            verts.reserve(N);

            for (int i = 0; i < N; i++) {
                pts[i] = { rx(rng), ry(rng) };
                verts.push_back(std::make_shared<Vertex>(
                        i,
                        pts[i].x,
                        pts[i].y,
                        "ap_" + std::to_string(i)
                ));
            }

            // ─────────────────────────────────────────────────────────────
            // Quadtree: build + query every node
            // ─────────────────────────────────────────────────────────────
            long long edgesQt = 0;
            double buildTime = 0.0;
            QuadtreeStats qtStats;

            double elapsedQt = timedAverage([&] {
                std::unique_ptr<Quadtree> qt;

                buildTime = timed([&] {
                    qt = std::make_unique<Quadtree>(verts);
                });

                for (int i = 0; i < N; i++) {
                    auto neighbours = qt->rangeSearch(
                            pts[i].x,
                            pts[i].y,
                            radius,
                            qtStats
                    );

                    for (auto* nb : neighbours) {
                        if ((int)nb->getId() > i) {
                            edgesQt++;
                        }
                    }
                }
            });

            double qtChecksPerSecond =
                    elapsedQt > 0.0
                    ? (double)qtStats.distanceChecks / elapsedQt
                    : 0.0;

            // ─────────────────────────────────────────────────────────────
            // Brute force: check every pair
            // ─────────────────────────────────────────────────────────────
            long long edgesBf = 0;
            long long bruteForceChecks = 0;

            double elapsedBf = timedAverage([&] {
                double r2 = radius * radius;

                for (int i = 0; i < N; i++) {
                    for (int j = i + 1; j < N; j++) {
                        bruteForceChecks++;

                        double dx = pts[i].x - pts[j].x;
                        double dy = pts[i].y - pts[j].y;

                        if (dx * dx + dy * dy <= r2) {
                            edgesBf++;
                        }
                    }
                }
            });

            double bfChecksPerSecond =
                    elapsedBf > 0.0
                    ? (double)bruteForceChecks / elapsedBf
                    : 0.0;

            double avgDegree = 2.0 * (double)edgesBf / (double)N;
            double timeSpeedup = elapsedBf / elapsedQt;
            double checkReduction =
                    qtStats.distanceChecks > 0
                    ? (double)bruteForceChecks / (double)qtStats.distanceChecks
                    : 0.0;

            bool sameEdges = edgesQt == edgesBf;
            bool quadtreeSlower = elapsedQt > elapsedBf;

            if (!foundBreakingPointForN && quadtreeSlower) {
                foundBreakingPointForN = true;
                globalFoundBreakingPoint = true;
                breakingAvgDegree = avgDegree;
                breakingRadius = radius;
            }

            // ─────────────────────────────────────────────────────────────
            // Row 1: distance checks
            // ─────────────────────────────────────────────────────────────
            {
                BenchmarkRow row;
                row.benchmark    = "quadtree";
                row.variant      = "Quadtree";
                row.paramN       = N;
                row.paramP       = avgDegree;
                row.elapsedS     = elapsedQt;
                row.quality      = (double)qtStats.distanceChecks;
                row.qualityLabel = "distance_checks";
                row.dnf          = !sameEdges;
                row.notes        = "radius=" + std::to_string(radius)
                                   + " edges_found=" + std::to_string(edgesQt)
                                   + " build_time_s=" + std::to_string(buildTime)
                                   + " checks_per_s=" + std::to_string(qtChecksPerSecond)
                                   + " box_checks=" + std::to_string(qtStats.boxChecks)
                                   + " nodes_visited=" + std::to_string(qtStats.nodesVisited)
                                   + " nodes_pruned=" + std::to_string(qtStats.nodesPruned)
                                   + " avg_degree=" + std::to_string(avgDegree)
                                   + " speedup=" + std::to_string(timeSpeedup)
                                   + " check_reduction=" + std::to_string(checkReduction);
                record(row);
            }

            {
                BenchmarkRow row;
                row.benchmark    = "quadtree";
                row.variant      = "BruteForce";
                row.paramN       = N;
                row.paramP       = avgDegree;
                row.elapsedS     = elapsedBf;
                row.quality      = (double)bruteForceChecks;
                row.qualityLabel = "distance_checks";
                row.dnf          = !sameEdges;
                row.notes        = "radius=" + std::to_string(radius)
                                   + " edges_found=" + std::to_string(edgesBf)
                                   + " checks_per_s=" + std::to_string(bfChecksPerSecond)
                                   + " avg_degree=" + std::to_string(avgDegree);
                record(row);
            }

            // ─────────────────────────────────────────────────────────────
            // Row 2: checks per second
            // ─────────────────────────────────────────────────────────────
            {
                BenchmarkRow row;
                row.benchmark    = "quadtree";
                row.variant      = "Quadtree";
                row.paramN       = N;
                row.paramP       = avgDegree;
                row.elapsedS     = elapsedQt;
                row.quality      = qtChecksPerSecond;
                row.qualityLabel = "checks_per_second";
                row.dnf          = !sameEdges;
                row.notes        = "radius=" + std::to_string(radius)
                                   + " distance_checks=" + std::to_string(qtStats.distanceChecks)
                                   + " avg_degree=" + std::to_string(avgDegree);
                record(row);
            }

            {
                BenchmarkRow row;
                row.benchmark    = "quadtree";
                row.variant      = "BruteForce";
                row.paramN       = N;
                row.paramP       = avgDegree;
                row.elapsedS     = elapsedBf;
                row.quality      = bfChecksPerSecond;
                row.qualityLabel = "checks_per_second";
                row.dnf          = !sameEdges;
                row.notes        = "radius=" + std::to_string(radius)
                                   + " distance_checks=" + std::to_string(bruteForceChecks)
                                   + " avg_degree=" + std::to_string(avgDegree);
                record(row);
            }

            std::cout << "    radius=" << std::fixed << std::setprecision(1) << radius
                      << "  avg_degree=" << std::setprecision(2) << avgDegree
                      << "  QT=" << std::setprecision(4) << elapsedQt << "s"
                      << "  BF=" << elapsedBf << "s"
                      << "  speedup=" << timeSpeedup << "x"
                      << "  QT_checks=" << qtStats.distanceChecks
                      << "  BF_checks=" << bruteForceChecks
                      << "  check_reduction=" << checkReduction << "x";

            if (!sameEdges) {
                std::cout << "  [EDGE MISMATCH]";
            }

            if (quadtreeSlower) {
                std::cout << "  [QT slower]";
            }

            std::cout << "\n";
        }

        if (foundBreakingPointForN) {
            std::cout << "  Breaking point for N=" << N
                      << ": radius≈" << breakingRadius
                      << ", avg_degree≈" << breakingAvgDegree
                      << "\n";
        } else {
            std::cout << "  No breaking point found for N=" << N
                      << " in this radius sweep.\n";
        }
    }

    if (!globalFoundBreakingPoint) {
        std::cout << "\n  No quadtree degradation point found in this sweep.\n";
    }

    std::cout << "  Done.\n";
}

void Benchmark::runPrim() {
    std::cout << "\n== Prim — FibonacciHeap vs MutablePriorityQueue ==\n";

    const std::vector<int> nodeCounts = {
            1000, 2000, 5000, 10000, 20000
    };

    const std::vector<int> avgDegrees = {
            4, 8, 16, 32
    };

    auto distance = [](const std::shared_ptr<Vertex>& a,
                       const std::shared_ptr<Vertex>& b) {
        double dx = a->getCoordinates().getX() - b->getCoordinates().getX();
        double dy = a->getCoordinates().getY() - b->getCoordinates().getY();
        return std::sqrt(dx * dx + dy * dy);
    };

    auto mstWeight = [](const std::vector<std::shared_ptr<Vertex>>& mst) {
        double total = 0.0;

        for (const auto& v : mst) {
            if (v->getPath() != nullptr) {
                total += v->getDist();
            }
        }

        return total;
    };

    for (int avgDegree : avgDegrees) {
        std::cout << "  avg_degree=" << avgDegree << "\n";

        for (int N : nodeCounts) {
            Multigraph g;

            std::mt19937 rng(cfg.pqRngSeed + N + avgDegree);
            std::uniform_real_distribution<double>
                    rx(cfg.coordMinX, cfg.coordMaxX),
                    ry(cfg.coordMinY, cfg.coordMaxY);

            std::vector<u_int> ids;
            ids.reserve(N);

            for (int i = 0; i < N; i++) {
                double x = rx(rng);
                double y = ry(rng);

                ids.push_back(g.addVertex(
                        x,
                        y,
                        "v_" + std::to_string(i)
                ));
            }

            const auto& verts = g.getVertexSet();

            // ----------------------------------------------------------------
            // Build a connected sparse graph.
            //
            // First add a chain to guarantee connectivity.
            // Then add random extra edges until approximate avg degree is reached.
            //
            // Undirected edge count target:
            //   E ~= N * avgDegree / 2
            // Chain already adds N - 1 edges.
            // ----------------------------------------------------------------
            std::set<std::pair<u_int, u_int>> usedEdges;

            auto addUndirectedEdge = [&](u_int a, u_int b) {
                if (a == b) return false;

                u_int x = std::min(a, b);
                u_int y = std::max(a, b);

                if (usedEdges.count({x, y})) {
                    return false;
                }

                usedEdges.insert({x, y});

                double w = distance(verts[a], verts[b]);

                g.addEdge(a, b, w, WALK);
                g.addEdge(b, a, w, WALK);

                return true;
            };

            for (int i = 0; i + 1 < N; i++) {
                addUndirectedEdge(i, i + 1);
            }

            long long targetUndirectedEdges =
                    std::max<long long>(N - 1, ((long long)N * avgDegree) / 2);

            std::uniform_int_distribution<int> vertexDist(0, N - 1);

            while ((long long)usedEdges.size() < targetUndirectedEdges) {
                u_int a = vertexDist(rng);
                u_int b = vertexDist(rng);
                addUndirectedEdge(a, b);
            }

            auto src = g.getVertex(0);

            // ----------------------------------------------------------------
            // Prim with Fibonacci Heap
            // ----------------------------------------------------------------
            double fibWeight = 0.0;
            bool fibDnf = false;

            double elapsedFib = timedAverage([&] {
                auto mst = g.prim(src, FIBONACCI_HEAP);
                fibWeight = mstWeight(mst);

                if ((int)mst.size() != N) {
                    fibDnf = true;
                }
            });

            {
                BenchmarkRow row;
                row.benchmark    = "prim";
                row.variant      = "Prim_FibHeap";
                row.paramN       = N;
                row.paramP       = avgDegree;
                row.elapsedS     = elapsedFib;
                row.quality      = fibWeight;
                row.qualityLabel = "mst_weight";
                row.dnf          = fibDnf;
                row.notes        = "undirected_edges=" + std::to_string(usedEdges.size()) +
                                   " avg_degree=" + std::to_string(avgDegree);
                record(row);
            }

            // ----------------------------------------------------------------
            // Prim with MutablePriorityQueue
            // ----------------------------------------------------------------
            double mutableWeight = 0.0;
            bool mutableDnf = false;

            double elapsedMutable = timedAverage([&] {
                auto mst = g.prim(src, MUTABLE_PRIORITY_QUEUE);
                mutableWeight = mstWeight(mst);

                if ((int)mst.size() != N) {
                    mutableDnf = true;
                }
            });

            {
                BenchmarkRow row;
                row.benchmark    = "prim";
                row.variant      = "Prim_MutablePQ";
                row.paramN       = N;
                row.paramP       = avgDegree;
                row.elapsedS     = elapsedMutable;
                row.quality      = mutableWeight;
                row.qualityLabel = "mst_weight";
                row.dnf          = mutableDnf;
                row.notes        = "undirected_edges=" + std::to_string(usedEdges.size()) +
                                   " avg_degree=" + std::to_string(avgDegree);
                record(row);
            }

            double speedup = elapsedMutable / elapsedFib;

            std::cout << "    N=" << N
                      << "  edges=" << usedEdges.size()
                      << "  Fib=" << std::fixed << std::setprecision(5) << elapsedFib << "s"
                      << "  Mutable=" << elapsedMutable << "s"
                      << "  Mutable/Fib=" << speedup << "x"
                      << "  MST weights: fib=" << fibWeight
                      << " mutable=" << mutableWeight;

            if (std::abs(fibWeight - mutableWeight) > 1e-6) {
                std::cout << "  [MST WEIGHT MISMATCH]";
            }

            std::cout << "\n";
        }
    }

    std::cout << "  Done.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// runAll
// ─────────────────────────────────────────────────────────────────────────────

void Benchmark::runAll() {
    std::cout << "\n=== EDAA Benchmark Suite ===\n"
              << "  Output: " << cfg.outputDir << "\n\n";

    //runDijkstraVsAstar();
    runPriorityQueues();
    //runColoring();
    //runQuadtree();
    //runPrim();

    writeAllCSVs();
    std::cout << "\nTotal rows: " << rows.size() << "  Done.\n\n";
}