#include "../header/App.h"

#include <algorithm>

#include "../header/Coloring.h"

#include <iostream>
#include <random>
#include <limits>
#include <cstdlib>
#include <iomanip>
#include <chrono>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

App::App() {}

void App::clearScreen() const {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void App::printHeader() const {
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║         Multigraph Explorer v1.0         ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";
    if (graphLoaded) {
        std::cout << "  Graph: " << multigraph.getVertexSet().size() << " vertices loaded\n";
    } else {
        std::cout << "  Graph: (none loaded)\n";
    }
    std::cout << "\n";
}

void App::waitEnter() const {
    std::cout << "\nPress Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
}

bool App::requireGraph() {
    if (!graphLoaded) {
        std::cout << "  No graph loaded. Please load a graph first.\n";
        waitEnter();
        return false;
    }
    return true;
}

int App::readInt(const std::string& prompt, int lo, int hi) {
    int val;
    while (true) {
        std::cout << prompt;
        if (std::cin >> val && val >= lo && val <= hi) {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return val;
        }
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "  Invalid — enter a number between " << lo << " and " << hi << ".\n";
    }
}

bool App::readYesNo(const std::string& prompt) {
    char c;
    std::cout << prompt << " [y/n]: ";
    std::cin >> c;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return c == 'y' || c == 'Y';
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertex / PQ / Mode pickers
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<Vertex> App::pickVertex(const std::string& prompt) {
    std::cout << "\n  " << prompt << "\n";
    std::cout << "  [1] By ID\n";
    std::cout << "  [2] By name (substring search)\n";
    int choice = readInt("  > ", 1, 2);

    if (choice == 1) {
        int maxId = (int)multigraph.getVertexSet().size() - 1;
        int id = readInt("  Vertex ID (0–" + std::to_string(maxId) + "): ", 0, maxId);
        return multigraph.getVertex((u_int)id);
    } else {
        std::cout << "  Name substring: ";
        std::string query;
        std::getline(std::cin, query);
        std::vector<std::shared_ptr<Vertex>> matches;
        for (auto& v : multigraph.getVertexSet()) {
            if (v->getName().find(query) != std::string::npos)
                matches.push_back(v);
        }
        if (matches.empty()) {
            std::cout << "  No matches found.\n";
            return nullptr;
        }
        std::cout << "  Matches:\n";
        int limit = std::min((int)matches.size(), 10);
        for (int i = 0; i < limit; i++)
            std::cout << "    [" << i << "] " << matches[i]->getId()
                      << " — " << matches[i]->getName() << "\n";
        int idx = readInt("  Select: ", 0, limit - 1);
        return matches[idx];
    }
}

PriorityQueueSelected App::pickPQ(const std::string& prompt) {
    std::cout << "\n  " << prompt << "\n";
    std::cout << "  [1] Mutable Priority Queue (binary heap)\n";
    std::cout << "  [2] Fibonacci Heap\n";
    std::cout << "  [3] Brute Force\n";
    int c = readInt("  > ", 1, 3);
    switch (c) {
        case 1: return MUTABLE_PRIORITY_QUEUE;
        case 2: return FIBONACCI_HEAP;
        default: return BRUTE_FORCE;
    }
}

std::set<Mode> App::pickModes() {
    std::set<Mode> modes;
    std::cout << "\n  Select allowed transport modes (toggle):\n";
    bool walk  = readYesNo("  Walk?");
    bool bus   = readYesNo("  Bus?");
    bool metro = readYesNo("  Metro?");
    if (walk)  modes.insert(WALK);
    if (bus)   modes.insert(BUS);
    if (metro) modes.insert(METRO);
    if (modes.empty()) {
        std::cout << "  No modes selected — defaulting to all.\n";
        modes = {WALK, BUS, METRO};
    }
    return modes;
}

// ─────────────────────────────────────────────────────────────────────────────
// Export & visualize
// ─────────────────────────────────────────────────────────────────────────────

void App::runVisualizer(const std::string& script) const {
    std::string cmd = PYTHON_INTERPRETER + " " + script;
    std::cout << "  Running visualizer: " << cmd << "\n";
    int ret = system(cmd.c_str());
    if (ret != 0)
        std::cout << "  Warning: visualizer exited with code " << ret << "\n";
    else
        std::cout << "  Saved to ../graph/prim_tree.png\n";
}

void App::exportAndVisualize(const std::vector<std::shared_ptr<Vertex>>& path, const std::string& label) {
    std::string csvPath = "../graph/algorithm_edges.csv";  // always same file
    multigraph.exportPathCSV(path, csvPath);
    std::cout << "  Exported to " << csvPath << "\n";
    if (readYesNo("  Run Python visualizer?"))
        runVisualizer(visualizerScript);
}

void App::printPath(const std::vector<std::shared_ptr<Vertex>>& path,
                    const std::string& label, double elapsed) {
    std::cout << "\n  ── " << label << " ──────────────────────────\n";
    std::cout << "  Elapsed : " << std::fixed << std::setprecision(4) << elapsed << "s\n";
    std::cout << "  Hops    : " << path.size() << "\n";

    // Distance is on the destination vertex after dijkstra_aux
    if (!path.empty()) {
        double dist = path.back()->getDist();
        // If getDist on last vertex wasn't set, fall back gracefully
        std::cout << "  Distance: " << std::fixed << std::setprecision(2) << dist << "\n";
    }

    // Print first 8 and last 8 vertices of the path
    std::cout << "  Path    : ";
    size_t n = path.size();
    size_t preview = 8;
    if (n <= preview * 2) {
        for (size_t i = 0; i < n; i++)
            std::cout << path[i]->getId() << (i + 1 < n ? " -> " : "");
    } else {
        for (size_t i = 0; i < preview; i++)
            std::cout << path[i]->getId() << " -> ";
        std::cout << "... -> ";
        for (size_t i = n - preview; i < n; i++)
            std::cout << path[i]->getId() << (i + 1 < n ? " -> " : "");
    }
    std::cout << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Graph loading
// ─────────────────────────────────────────────────────────────────────────────

void App::loadFromFiles(const std::string& nodesPath, const std::string& edgesPath) {
    std::cout << "  Loading from:\n    " << nodesPath << "\n    " << edgesPath << "\n";
    multigraph = Multigraph();
    FileParser parser(nodesPath, edgesPath);
    auto t0 = std::chrono::high_resolution_clock::now();
    parser.parse(multigraph);
    auto t1 = std::chrono::high_resolution_clock::now();
    graphLoaded = true;
    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "  Loaded " << multigraph.getVertexSet().size()
              << " vertices in " << std::fixed << std::setprecision(3) << elapsed << "s\n";
}

void App::loadRandom() {
    int V    = readInt("  Vertices (e.g. 100000): ", 2, 2000000);
    int deg  = readInt("  Avg edges per vertex (e.g. 8): ", 2, 50);
    int seed = readInt("  Random seed (e.g. 42): ", 0, 999999);

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> coordX(526000.0, 536000.0);
    std::uniform_real_distribution<double> coordY(4554000.0, 4560000.0);
    std::uniform_int_distribution<int>     modeD(0, 2);
    std::uniform_int_distribution<int>     vtxD(0, V - 1);

    static const double SPEEDS[] = { SPEED_WALK, SPEED_BUS, SPEED_METRO };

    multigraph = Multigraph();
    auto t0 = std::chrono::high_resolution_clock::now();

    // Step 1 — generate all coordinates upfront
    std::cout << "  Generating " << V << " vertices...\n";
    std::vector<std::pair<double,double>> coords(V);
    for (int i = 0; i < V; i++) {
        coords[i] = { coordX(rng), coordY(rng) };
        multigraph.addVertex(coords[i].first, coords[i].second, "v" + std::to_string(i));
    }

    // Step 2 — compute weight from spatial distance / mode speed
    auto makeWeight = [&](int a, int b, Mode mode) -> double {
        double dx = coords[a].first  - coords[b].first;
        double dy = coords[a].second - coords[b].second;
        double dist = std::sqrt(dx * dx + dy * dy);
        return dist / SPEEDS[static_cast<int>(mode)];  // seconds
    };

    // Spanning tree first — guarantees connectivity
    std::cout << "  Building spanning tree...\n";
    for (int i = 1; i < V; i++) {
        int  j    = std::uniform_int_distribution<int>(0, i - 1)(rng);
        Mode mode = static_cast<Mode>(modeD(rng));
        multigraph.addEdge(i, j, makeWeight(i, j, mode), mode);
    }

    // Extra random edges
    std::cout << "  Adding extra edges...\n";
    int target = V * deg / 2 - (V - 1);
    std::set<std::pair<int,int>> seen;
    for (int k = 0; k < target * 5 && (int)seen.size() < target; k++) {
        int a = vtxD(rng), b = vtxD(rng);
        if (a == b) continue;
        if (a > b) std::swap(a, b);
        if (seen.count({a, b})) continue;
        seen.insert({a, b});
        Mode mode = static_cast<Mode>(modeD(rng));
        multigraph.addEdge(a, b, makeWeight(a, b, mode), mode);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    graphLoaded = true;
    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "  Built " << V << " vertices, ~"
              << (V - 1 + (int)seen.size()) << " edges in "
              << std::fixed << std::setprecision(3) << elapsed << "s\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Algorithms
// ─────────────────────────────────────────────────────────────────────────────

void App::runDijkstra() {
    if (!requireGraph()) return;

    auto src  = pickVertex("Select SOURCE vertex");
    if (!src) { waitEnter(); return; }
    auto dest = pickVertex("Select DESTINATION vertex");
    if (!dest) { waitEnter(); return; }
    auto pqs  = pickPQ("Select priority queue");

    auto t0 = std::chrono::high_resolution_clock::now();
    auto path = multigraph.dijkstra(src, dest, pqs);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    printPath(path, "Dijkstra", elapsed);

    if (readYesNo("\n  Export path?"))
        exportAndVisualize(path, "Dijkstra");

    waitEnter();
}

void App::runAstar() {
    if (!requireGraph()) return;

    auto src  = pickVertex("Select SOURCE vertex");
    if (!src) { waitEnter(); return; }
    auto dest = pickVertex("Select DESTINATION vertex");
    if (!dest) { waitEnter(); return; }
    auto pqs  = pickPQ("Select priority queue");

    auto t0   = std::chrono::high_resolution_clock::now();
    auto path = multigraph.astar(src, dest, pqs);
    auto t1   = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    printPath(path, "A*", elapsed);

    if (readYesNo("\n  Export path?"))
        exportAndVisualize(path, "Astar");

    waitEnter();
}

void App::runColoring() {
    clearScreen();
    printHeader();

    std::cout << "  Graph Coloring - Channel Assignment\n\n";
    std::cout << "  [1] COLORING/coloring.json\n";
    std::cout << "  [2] COLORING/coloring_benchmark.json\n";
    std::cout << "  [3] COLORING/coloring_benchmark_harder.json\n";
    std::cout << "  [4] Custom JSON path\n";
    std::cout << "  [0] Back\n\n";

    int fileChoice = readInt("  > ", 0, 4);
    if (fileChoice == 0) return;

    std::string jsonPath;
    switch (fileChoice) {
        case 1: jsonPath = "../COLORING/coloring.json"; break;
        case 2: jsonPath = "../COLORING/coloring_benchmark.json"; break;
        case 3: jsonPath = "../COLORING/coloring_benchmark_harder.json"; break;
        case 4:
            std::cout << "  JSON path: ";
            std::getline(std::cin, jsonPath);
            break;
    }

    std::cout << "\n  Select coloring algorithm\n";
    std::cout << "  [1] Brute Force exact coloring\n";
    std::cout << "  [2] Welsh-Powell heuristic\n";
    std::cout << "  [3] Run both\n";

    int algChoice = readInt("  > ", 1, 3);

    try {
        Coloring coloring;

        auto loadT0 = std::chrono::high_resolution_clock::now();
        coloring.loadFromJson(jsonPath);
        auto loadT1 = std::chrono::high_resolution_clock::now();

        double loadElapsed =
                std::chrono::duration<double>(loadT1 - loadT0).count();

        std::cout << "  Antennas       : " << coloring.getAntennaCount() << "\n";
        std::cout << "  Conflict edges : " << coloring.getConflictEdgeCount() << "\n";

        double n = coloring.getAntennaCount();
        double maxEdges = n * (n - 1.0) / 2.0;
        double density = coloring.getConflictEdgeCount() / maxEdges;

        std::cout << "  Density        : "
                  << std::fixed << std::setprecision(4)
                  << density * 100.0 << "%\n";

        std::cout << "\n  Loaded coloring instance from " << jsonPath << "\n";
        std::cout << "  Antennas            : " << coloring.getAntennaCount() << "\n";
        std::cout << "  Conflict edges      : " << coloring.getConflictEdgeCount() << "\n";
        std::cout << "  Interference radius : "
                  << std::fixed << std::setprecision(2)
                  << coloring.getInterferenceRadius() << "\n";
        std::cout << "  Load/build time     : "
                  << std::fixed << std::setprecision(5)
                  << loadElapsed << "s\n";

        Coloring::Solution bruteSol;
        Coloring::Solution heuristicSol;

        bool hasBrute = false;
        bool hasHeuristic = false;

        if (algChoice == 1 || algChoice == 3) {
            if (coloring.getAntennaCount() > 22) {
                std::cout << "\n  Warning: brute force with "
                          << coloring.getAntennaCount()
                          << " antennas can be very slow.\n";

                if (!readYesNo("  Continue with brute force?")) {
                    if (algChoice == 1) {
                        waitEnter();
                        return;
                    }
                } else {
                    std::cout << "\n  Running Brute Force...\n";

                    auto t0 = std::chrono::high_resolution_clock::now();
                    bruteSol = coloring.solveBruteForce();
                    auto t1 = std::chrono::high_resolution_clock::now();

                    double elapsed =
                            std::chrono::duration<double>(t1 - t0).count();

                    std::cout << "\n  ── Coloring Brute Force ────────────────\n";
                    std::cout << "  Elapsed    : "
                              << std::fixed << std::setprecision(5)
                              << elapsed << "s\n";
                    coloring.printSolution(bruteSol);

                    hasBrute = true;
                }
            } else {
                std::cout << "\n  Running Brute Force...\n";

                auto t0 = std::chrono::high_resolution_clock::now();
                bruteSol = coloring.solveBruteForce();
                auto t1 = std::chrono::high_resolution_clock::now();

                double elapsed =
                        std::chrono::duration<double>(t1 - t0).count();

                std::cout << "\n  ── Coloring Brute Force ────────────────\n";
                std::cout << "  Elapsed    : "
                          << std::fixed << std::setprecision(5)
                          << elapsed << "s\n";
                coloring.printSolution(bruteSol);

                hasBrute = true;
            }
        }

        if (algChoice == 2 || algChoice == 3) {
            std::cout << "\n  Running Welsh-Powell heuristic...\n";

            auto t0 = std::chrono::high_resolution_clock::now();
            heuristicSol = coloring.solveWelshPowell();
            auto t1 = std::chrono::high_resolution_clock::now();

            double elapsed =
                    std::chrono::duration<double>(t1 - t0).count();

            std::cout << "\n  ── Coloring Welsh-Powell ───────────────\n";
            std::cout << "  Elapsed    : "
                      << std::fixed << std::setprecision(5)
                      << elapsed << "s\n";
            coloring.printSolution(heuristicSol);

            hasHeuristic = true;
        }

        Coloring::Solution exportSol;

        if (hasBrute && bruteSol.feasible) {
            exportSol = bruteSol;
            std::cout << "\n  Exporting Brute Force solution.\n";
        } else if (hasHeuristic && heuristicSol.feasible) {
            exportSol = heuristicSol;
            std::cout << "\n  Exporting Welsh-Powell solution.\n";
        } else {
            std::cout << "\n  No feasible solution to export.\n";
            waitEnter();
            return;
        }

        coloring.exportCSV(
                exportSol,
                "../graph/coloring_vertices.csv",
                "../graph/coloring_edges.csv"
        );

        std::cout << "  Exported vertices to ../graph/coloring_vertices.csv\n";
        std::cout << "  Exported edges to ../graph/coloring_edges.csv\n";

        runVisualizer("../graph/Coloring_visualizer.py");

    } catch (const std::exception& e) {
        std::cout << "\n  Coloring error: " << e.what() << "\n";
    }

    waitEnter();
}

void App::runDijkstraFilter() {
    if (!requireGraph()) return;

    auto src  = pickVertex("Select SOURCE vertex");
    if (!src) { waitEnter(); return; }
    auto dest = pickVertex("Select DESTINATION vertex");
    if (!dest) { waitEnter(); return; }
    auto modes = pickModes();
    auto pqs   = pickPQ("Select priority queue");

    auto t0 = std::chrono::high_resolution_clock::now();
    auto path = multigraph.dijkstra_filter(src, dest, modes, pqs);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    printPath(path, "Dijkstra (filtered)", elapsed);

    if (readYesNo("\n  Export path?"))
        exportAndVisualize(path, "DijkstraFiltered");

    waitEnter();
}

void App::runPrim() {
    if (!requireGraph()) return;

    auto src = pickVertex("Select START vertex for Prim's MST");
    if (!src) { waitEnter(); return; }
    auto pqs = pickPQ("Select priority queue");

    auto t0  = std::chrono::high_resolution_clock::now();
    auto mst = multigraph.prim(src, pqs);
    auto t1  = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "\n  ── Prim's MST ──────────────────────────\n";
    std::cout << "  Elapsed    : " << std::fixed << std::setprecision(4) << elapsed << "s\n";
    std::cout << "  MST nodes  : " << mst.size() << "\n";

    double totalWeight = 0.0;
    for (auto& v : mst) totalWeight += v->getDist();
    std::cout << "  Total weight: " << std::fixed << std::setprecision(2) << totalWeight << "\n";

    if (readYesNo("\n  Export MST edges?"))
        exportAndVisualize(mst, "Prim");

    waitEnter();
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmarks
// ─────────────────────────────────────────────────────────────────────────────

void App::benchmarkDijkstra() {
    if (!requireGraph()) return;

    auto src  = pickVertex("Select SOURCE vertex");
    if (!src) { waitEnter(); return; }
    auto dest = pickVertex("Select DESTINATION vertex");
    if (!dest) { waitEnter(); return; }
    int runs  = readInt("  Number of runs per PQ: ", 1, 100);

    bool skipBF = multigraph.getVertexSet().size() > 50000;
    if (skipBF)
        std::cout << "  Note: Brute force skipped (graph > 50k vertices — would take too long).\n";

    struct Result { std::string name; double elapsed; size_t hops; double dist; };
    std::vector<Result> results;

    auto bench = [&](const std::string& name, PriorityQueueSelected pqs) {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<std::shared_ptr<Vertex>> path;
        for (int i = 0; i < runs; i++)
            path = multigraph.dijkstra(src, dest, pqs);
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count() / runs;
        double dist = path.empty() ? 0.0 : dest->getDist();
        results.push_back({name, elapsed, path.size(), dist});
        std::cout << "  " << name << " done.\n";
    };

    std::cout << "\n  Running benchmarks (" << runs << " run(s) each)...\n";
    bench("MutablePriorityQueue", MUTABLE_PRIORITY_QUEUE);
    bench("FibonacciHeap",        FIBONACCI_HEAP);
    if (!skipBF) bench("BruteForce", BRUTE_FORCE);

    // Find fastest
    auto fastest = std::min_element(results.begin(), results.end(),
                                    [](const Result& a, const Result& b){ return a.elapsed < b.elapsed; });

    std::cout << "\n  ── Benchmark Results (" << runs << " run avg) ──────\n";
    std::cout << std::left
              << std::setw(26) << "  Queue"
              << std::setw(14) << "Time (s)"
              << std::setw(10) << "Hops"
              << "Distance\n";
    std::cout << "  " << std::string(58, '-') << "\n";
    for (auto& r : results) {
        bool win = (r.name == fastest->name);
        std::cout << "  " << std::setw(24) << r.name
                  << std::setw(14) << std::fixed << std::setprecision(5) << r.elapsed
                  << std::setw(10) << r.hops
                  << std::fixed << std::setprecision(2) << r.dist
                  << (win ? "  ← fastest" : "") << "\n";
    }

    if (results.size() > 1) {
        double ref = fastest->elapsed;
        std::cout << "\n  Speedup vs fastest:\n";
        for (auto& r : results) {
            if (r.name == fastest->name) continue;
            std::cout << "    " << r.name << " is "
                      << std::fixed << std::setprecision(2) << r.elapsed / ref
                      << "x slower\n";
        }
    }

    waitEnter();
}

void App::benchmarkAstarVsDijkstra() {
    if (!requireGraph()) return;

    auto src  = pickVertex("Select SOURCE vertex");
    if (!src) { waitEnter(); return; }
    auto dest = pickVertex("Select DESTINATION vertex");
    if (!dest) { waitEnter(); return; }
    auto pqs  = pickPQ("Select priority queue (same for both)");
    int runs  = readInt("  Number of runs: ", 1, 100);

    struct Result { std::string name; double elapsed; size_t hops; double dist; };
    std::vector<Result> results;

    auto bench = [&](const std::string& name, auto fn) {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<std::shared_ptr<Vertex>> path;
        for (int i = 0; i < runs; i++) path = fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count() / runs;
        results.push_back({name, elapsed, path.size(), dest->getDist()});
        std::cout << "  " << name << " done.\n";
    };

    std::cout << "\n  Running...\n";
    bench("Dijkstra", [&]{ return multigraph.dijkstra(src, dest, pqs); });
    bench("A*",       [&]{ return multigraph.astar(src, dest, pqs); });

    auto fastest = std::min_element(results.begin(), results.end(),
                                    [](const Result& a, const Result& b){ return a.elapsed < b.elapsed; });

    std::cout << "\n  ── A* vs Dijkstra (" << runs << " run avg) ────────\n";
    std::cout << std::left
              << std::setw(14) << "  Algorithm"
              << std::setw(14) << "Time (s)"
              << std::setw(10) << "Hops"
              << "Distance\n";
    std::cout << "  " << std::string(46, '-') << "\n";
    for (auto& r : results) {
        std::cout << "  " << std::setw(12) << r.name
                  << std::setw(14) << std::fixed << std::setprecision(5) << r.elapsed
                  << std::setw(10) << r.hops
                  << std::fixed << std::setprecision(2) << r.dist
                  << (r.name == fastest->name ? "  ← fastest" : "") << "\n";
    }
    double speedup = results[0].elapsed / results[1].elapsed;
    std::cout << "\n  A* speedup over Dijkstra: "
              << std::fixed << std::setprecision(2) << speedup << "x\n";

    waitEnter();
}

void App::benchmarkPrim() {
    if (!requireGraph()) return;

    auto src = pickVertex("Select START vertex");
    if (!src) { waitEnter(); return; }
    int runs = readInt("  Number of runs per PQ: ", 1, 20);

    bool skipBF = multigraph.getVertexSet().size() > 10000;
    if (skipBF)
        std::cout << "  Note: Brute force skipped (graph > 10k vertices).\n";

    struct Result { std::string name; double elapsed; double weight; };
    std::vector<Result> results;

    auto bench = [&](const std::string& name, PriorityQueueSelected pqs) {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<std::shared_ptr<Vertex>> mst;
        for (int i = 0; i < runs; i++)
            mst = multigraph.prim(src, pqs);
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count() / runs;
        double w = 0; for (auto& v : mst) w += v->getDist();
        results.push_back({name, elapsed, w});
        std::cout << "  " << name << " done.\n";
    };

    std::cout << "\n  Running Prim benchmarks (" << runs << " run(s) each)...\n";
    bench("MutablePriorityQueue", MUTABLE_PRIORITY_QUEUE);
    bench("FibonacciHeap",        FIBONACCI_HEAP);
    if (!skipBF) bench("BruteForce", BRUTE_FORCE);

    auto fastest = std::min_element(results.begin(), results.end(),
                                    [](const Result& a, const Result& b){ return a.elapsed < b.elapsed; });

    std::cout << "\n  ── Prim Benchmark Results ──────────────\n";
    std::cout << std::left
              << std::setw(26) << "  Queue"
              << std::setw(14) << "Time (s)"
              << "MST Weight\n";
    std::cout << "  " << std::string(50, '-') << "\n";
    for (auto& r : results) {
        bool win = (r.name == fastest->name);
        std::cout << "  " << std::setw(24) << r.name
                  << std::setw(14) << std::fixed << std::setprecision(5) << r.elapsed
                  << std::fixed << std::setprecision(2) << r.weight
                  << (win ? "  ← fastest" : "") << "\n";
    }

    waitEnter();
}

void App::benchmarkColoring() {
    clearScreen();
    printHeader();

    std::cout << "  Coloring Benchmark - Brute Force vs Welsh-Powell\n\n";
    std::cout << "  [1] COLORING/coloring.json\n";
    std::cout << "  [2] COLORING/coloring_benchmark.json\n";
    std::cout << "  [3] COLORING/coloring_benchmark_harder.json\n";
    std::cout << "  [4] Custom JSON path\n";
    std::cout << "  [0] Back\n\n";

    int fileChoice = readInt("  > ", 0, 4);
    if (fileChoice == 0) return;

    std::string jsonPath;
    switch (fileChoice) {
        case 1: jsonPath = "../COLORING/coloring.json"; break;
        case 2: jsonPath = "../COLORING/coloring_benchmark.json"; break;
        case 3: jsonPath = "../COLORING/coloring_benchmark_harder.json"; break;
        case 4:
            std::cout << "  JSON path: ";
            std::getline(std::cin, jsonPath);
            break;
    }

    try {
        Coloring coloring;

        auto loadT0 = std::chrono::high_resolution_clock::now();
        coloring.loadFromJson(jsonPath);
        auto loadT1 = std::chrono::high_resolution_clock::now();

        double loadElapsed =
                std::chrono::duration<double>(loadT1 - loadT0).count();

        std::cout << "\n  Loaded coloring benchmark instance\n";
        std::cout << "  File                : " << jsonPath << "\n";
        std::cout << "  Antennas            : " << coloring.getAntennaCount() << "\n";
        std::cout << "  Conflict edges      : " << coloring.getConflictEdgeCount() << "\n";
        std::cout << "  Interference radius : "
                  << std::fixed << std::setprecision(2)
                  << coloring.getInterferenceRadius() << "\n";
        std::cout << "  Load/build time     : "
                  << std::fixed << std::setprecision(5)
                  << loadElapsed << "s\n";

        struct Result {
            std::string name;
            double elapsed;
            int colorsUsed;
            bool feasible;
            Coloring::Solution solution;
        };

        std::vector<Result> results;

        bool runBrute = true;

        if (coloring.getAntennaCount() > 22) {
            std::cout << "\n  Brute force warning: "
                      << coloring.getAntennaCount()
                      << " antennas can be very slow.\n";

            runBrute = readYesNo("  Run brute force anyway?");
        }

        if (runBrute) {
            std::cout << "\n  Running Brute Force...\n";

            auto t0 = std::chrono::high_resolution_clock::now();
            auto sol = coloring.solveBruteForce();
            auto t1 = std::chrono::high_resolution_clock::now();

            double elapsed =
                    std::chrono::duration<double>(t1 - t0).count();

            results.push_back({
                                      "Brute Force",
                                      elapsed,
                                      sol.colorsUsed,
                                      sol.feasible,
                                      sol
                              });

            std::cout << "  Brute Force done.\n";
        } else {
            std::cout << "  Brute Force skipped.\n";
        }

        std::cout << "\n  Running Welsh-Powell heuristic...\n";

        auto h0 = std::chrono::high_resolution_clock::now();
        auto heuristicSol = coloring.solveWelshPowell();
        auto h1 = std::chrono::high_resolution_clock::now();

        double heuristicElapsed =
                std::chrono::duration<double>(h1 - h0).count();

        results.push_back({
                                  "Welsh-Powell",
                                  heuristicElapsed,
                                  heuristicSol.colorsUsed,
                                  heuristicSol.feasible,
                                  heuristicSol
                          });

        std::cout << "  Welsh-Powell done.\n";

        std::cout << "\n  ── Coloring Benchmark Results ─────────\n";
        std::cout << std::left
                  << std::setw(18) << "  Algorithm"
                  << std::setw(14) << "Time (s)"
                  << std::setw(12) << "Colors"
                  << "Valid\n";

        std::cout << "  " << std::string(50, '-') << "\n";

        for (const auto& r : results) {
            std::cout << "  "
                      << std::setw(16) << r.name
                      << std::setw(14)
                      << std::fixed << std::setprecision(6)
                      << r.elapsed
                      << std::setw(12)
                      << r.colorsUsed
                      << (r.feasible ? "yes" : "no")
                      << "\n";
        }

        if (results.size() == 2 &&
            results[0].feasible &&
            results[1].feasible &&
            results[0].colorsUsed > 0 &&
            results[1].colorsUsed > 0) {

            const Result& brute = results[0];
            const Result& heuristic = results[1];

            double speedup = brute.elapsed / heuristic.elapsed;
            double gap =
                    ((double)heuristic.colorsUsed - brute.colorsUsed)
                    / brute.colorsUsed * 100.0;

            std::cout << "\n  Welsh-Powell speedup over brute force: "
                      << std::fixed << std::setprecision(2)
                      << speedup << "x\n";

            std::cout << "  Welsh-Powell color gap vs optimal: "
                      << std::fixed << std::setprecision(2)
                      << gap << "%\n";
        }

        Coloring::Solution exportSol;
        bool foundExport = false;

        for (const auto& r : results) {
            if (r.name == "Brute Force" && r.feasible) {
                exportSol = r.solution;
                foundExport = true;
                std::cout << "\n  Exporting Brute Force solution.\n";
                break;
            }
        }

        if (!foundExport) {
            for (const auto& r : results) {
                if (r.name == "Welsh-Powell" && r.feasible) {
                    exportSol = r.solution;
                    foundExport = true;
                    std::cout << "\n  Exporting Welsh-Powell solution.\n";
                    break;
                }
            }
        }

        if (foundExport) {
            coloring.exportCSV(
                    exportSol,
                    "../graph/coloring_vertices.csv",
                    "../graph/coloring_edges.csv"
            );

            std::cout << "  Exported vertices to ../graph/coloring_vertices.csv\n";
            std::cout << "  Exported edges to ../graph/coloring_edges.csv\n";

            runVisualizer("../graph/Coloring_visualizer.py");
        } else {
            std::cout << "\n  No feasible solution to export.\n";
        }

    } catch (const std::exception& e) {
        std::cout << "\n  Coloring benchmark error: " << e.what() << "\n";
    }

    waitEnter();
}

// ─────────────────────────────────────────────────────────────────────────────
// Menus
// ─────────────────────────────────────────────────────────────────────────────

void App::menuLoadGraph() {
    clearScreen();
    printHeader();
    std::cout << "  Load Graph\n\n";
    std::cout << "  [1] Load from default files\n";
    std::cout << "        " << defaultNodesPath << "\n";
    std::cout << "        " << defaultEdgesPath << "\n";
    std::cout << "  [2] Load from custom file paths\n";
    std::cout << "  [3] Generate random graph\n";
    std::cout << "  [0] Back\n\n";

    int c = readInt("  > ", 0, 3);
    switch (c) {
        case 1:
            loadFromFiles(defaultNodesPath, defaultEdgesPath);
            waitEnter();
            break;
        case 2: {
            std::string np, ep;
            std::cout << "  Nodes CSV path: "; std::getline(std::cin, np);
            std::cout << "  Edges CSV path: "; std::getline(std::cin, ep);
            loadFromFiles(np, ep);
            waitEnter();
            break;
        }
        case 3:
            loadRandom();
            waitEnter();
            break;
        default: break;
    }
}

void App::menuAlgorithms() {
    clearScreen();
    printHeader();
    std::cout << "  Run Algorithm\n\n";
    std::cout << "  [1] Dijkstra (shortest path)\n";
    std::cout << "  [2] Dijkstra with mode filter\n";
    std::cout << "  [3] Prim's MST\n";
    std::cout << "  [4] A* (shortest path)\n";
    std::cout << "  [5] COLORING (welsh powell & Brute force)\n";
    std::cout << "  [0] Back\n\n";

    int c = readInt("  > ", 0, 5);
    switch (c) {
        case 1: runDijkstra();       break;
        case 2: runDijkstraFilter(); break;
        case 3: runPrim();           break;
        case 4: runAstar();          break;
        case 5: runColoring();            break;
        default: break;
    }
}

void App::menuBenchmark() {
    clearScreen();
    printHeader();
    std::cout << "  Benchmark\n\n";
    std::cout << "  [1] Dijkstra — compare all priority queues\n";
    std::cout << "  [2] Prim    — compare all priority queues\n";
    std::cout << "  [3] A* vs Dijkstra\n";
    std::cout << "  [4] COLORING - Benchmark welsh powell vs brute force\n";
    std::cout << "  [0] Back\n\n";

    int c = readInt("  > ", 0, 4);
    switch (c) {
        case 1: benchmarkDijkstra(); break;
        case 2: benchmarkPrim();     break;
        case 3: benchmarkAstarVsDijkstra(); break;
        case 4: benchmarkColoring(); break;
        default: break;
    }
}

void App::menuMain() {
    while (true) {
        clearScreen();
        printHeader();
        std::cout << "  [1] Load / build graph\n";
        std::cout << "  [2] Run algorithm\n";
        std::cout << "  [3] Benchmark priority queues\n";
        std::cout << "  [0] Exit\n\n";

        int c = readInt("  > ", 0, 3);
        switch (c) {
            case 1: menuLoadGraph();   break;
            case 2: menuAlgorithms();  break;
            case 3: menuBenchmark();   break;
            case 0: return;
        }
    }
}

void App::run() {
    menuMain();
}
