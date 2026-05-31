#include "../header/Benchmark.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#if defined(ENABLE_BENCHMARK_CACHE) && defined(__linux__)
#include <asm/unistd.h>
    #include <linux/perf_event.h>
    #include <sys/ioctl.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

#if defined(__linux__)
#include <unistd.h>
#endif

Benchmark::Benchmark() : cfg(Config{}) {}

Benchmark::Benchmark(Config config) : cfg(std::move(config)) {}

void Benchmark::run() {
    ensureDirectory(cfg.outputDirectory);
    if (cfg.runPriorityQueues) runPriorityQueueBenchmarks();
    if (cfg.runInterferenceGraph) runInterferenceGraphBenchmarks();
    if (cfg.runColoring) runColoringBenchmarks();
    if (cfg.runShortestPath) runShortestPathBenchmarks();
    if (cfg.runPrim) runPrimBenchmarks();
}

void Benchmark::runPriorityQueueBenchmarks() {
    ensureDirectory(cfg.outputDirectory);
    const std::string file = cfg.outputDirectory + "/priority_queues.csv";
    const std::string header =
            "seed,repetition,n,average_degree,pq,time_ms,cache_misses,cache_references,rss_before_bytes,rss_after_bytes,"
            "rss_delta_bytes,inserts,deletes,decrease_keys,dest_dist,path_checksum,matches_reference";

    std::cout << "Running Priority Queue Benchmarks\n";

    const std::vector<PriorityQueueSelected> queues = {BRUTE_FORCE, MUTABLE_PRIORITY_QUEUE, FIBONACCI_HEAP};

    for (std::size_t n : cfg.priorityQueues.graphSizes) {
        for (double conn : cfg.priorityQueues.averageDegrees) {

            const unsigned totalRuns = cfg.priorityQueues.warmupRuns + cfg.priorityQueues.repetitions;

            for (unsigned run = 0; run < totalRuns; ++run) {
                const bool isWarmup = run < cfg.priorityQueues.warmupRuns;
                const unsigned rep = isWarmup ? 0 : run - cfg.priorityQueues.warmupRuns;

                const std::uint32_t seed = cfg.priorityQueues.seed + rep;
                double referenceDist = std::numeric_limits<double>::quiet_NaN();
                std::uint64_t referenceChecksum = 0;

                for (PriorityQueueSelected pq : queues) {
                    std::cout << "Run: " << run << " - Number of nodes: " << n << " - average_degree: " << conn << " - " << pqName(pq) << "\n";
                    Multigraph g = generateGraph(n, conn, seed);
                    const auto src = g.getVertex(0);
                    const auto dst = g.getVertex((u_int)n - 1);

                    PriorityQueue::resetStats();
                    Measurement m = measure([&] { g.prim(src, pq); }, cfg.priorityQueues.cache.enabled);
                    PriorityQueueStats stats = PriorityQueue::getStats();

                    double dist = pathCostOrDist(g, n - 1);
                    std::uint64_t checksum = checksumPath(g);
                    bool matches = true;
                    if (pq == queues.front()) {
                        referenceDist = dist;
                        referenceChecksum = checksum;
                    } else {
                        matches = std::abs(dist - referenceDist) <= 1e-7 && checksum == referenceChecksum;
                    }

                    if (isWarmup) {
                        continue;
                    }

                    std::ostringstream line;
                    line << seed << ',' << rep << ',' << n << ',' << conn << ',' << pqName(pq) << ','
                         << std::fixed << std::setprecision(6) << m.milliseconds << ','
                         << m.cacheMisses << ',' << m.cacheReferences << ','
                         << m.rssBeforeBytes << ',' << m.rssAfterBytes << ','
                         << static_cast<long long>(m.rssAfterBytes) - static_cast<long long>(m.rssBeforeBytes) << ','
                         << stats.inserts << ',' << stats.deletes << ',' << stats.updateKeys << ','
                         << std::setprecision(12) << dist << ',' << checksum << ',' << (matches ? 1 : 0);
                    appendLine(file, header, line.str());
                }
            }
        }
    }
}

void Benchmark::runInterferenceGraphBenchmarks() {
    ensureDirectory(cfg.outputDirectory);
    const std::string file = cfg.outputDirectory + "/interference_graph.csv";
    const std::string header =
            "seed,repetition,n,radius,approach,total_time_ms,build_time_ms,run_time_ms,cache_misses,cache_references,"
            "rss_before_bytes,rss_after_bytes,rss_delta_bytes,estimated_structure_bytes,edges,edge_checksum,matches_reference,"
            "nodes_visited,nodes_pruned,box_checks,distance_checks,checks_per_second";

    std::cout << "Running Interference graph Benchmarks\n";

    for (std::size_t n : cfg.interference.nodeCounts) {
        for (double radius : cfg.interference.radii) {

            const unsigned totalRuns = cfg.interference.warmupRuns + cfg.interference.repetitions;

            for (unsigned run = 0; run < totalRuns; ++run) {
                const bool isWarmup = run < cfg.interference.warmupRuns;
                const unsigned rep = isWarmup ? 0 : run - cfg.interference.warmupRuns;

                const std::uint32_t seed = cfg.interference.seed + rep;
                auto points = generatePoints(n, cfg.interference.coordinateMax, seed);

                QuadtreeStats bruteStats{};
                std::vector<std::pair<std::size_t, std::size_t>> bruteEdges;

                std::cout << "Run: " << run << " - Number of nodes: " << n << " - Brute force\n";
                Measurement bruteM = measure([&] {
                    bruteEdges = buildInterferenceBruteForce(points, radius, bruteStats);
                }, cfg.interference.cache.enabled);

                const auto bruteChecksum = edgeChecksum(bruteEdges);
                const double bruteChecksPerSec = bruteM.milliseconds > 0.0
                                                 ? (1000.0 * static_cast<double>(bruteStats.distanceChecks) / bruteM.milliseconds)
                                                 : 0.0;

                QuadtreeStats qtStats{};
                std::vector<std::pair<std::size_t, std::size_t>> qtEdges;
                double qtBuildMs = 0.0;
                std::size_t qtBytes = 0;

                std::cout << "Run: " << run << " - Number of nodes: " << n << " - Quadtree force\n";

                Measurement qtM = measure([&] {
                    qtEdges = buildInterferenceQuadtree(points, radius, qtStats, qtBuildMs, qtBytes);
                }, cfg.interference.cache.enabled);

                const auto qtChecksum = edgeChecksum(qtEdges);
                const bool matches = qtChecksum == bruteChecksum && qtEdges.size() == bruteEdges.size();
                const double qtRunMs = std::max(0.0, qtM.milliseconds - qtBuildMs);
                const double qtChecksPerSec = qtRunMs > 0.0
                                              ? (1000.0 * static_cast<double>(qtStats.distanceChecks) / qtRunMs)
                                              : 0.0;

                if (isWarmup) {
                    continue;
                }

                {
                    std::ostringstream line;
                    line << seed << ',' << rep << ',' << n << ',' << radius << ",brute_force,"
                         << std::fixed << std::setprecision(6)
                         << bruteM.milliseconds << ",0," << bruteM.milliseconds << ','
                         << bruteM.cacheMisses << ',' << bruteM.cacheReferences << ','
                         << bruteM.rssBeforeBytes << ',' << bruteM.rssAfterBytes << ','
                         << static_cast<long long>(bruteM.rssAfterBytes) - static_cast<long long>(bruteM.rssBeforeBytes) << ','
                         << (points.size() * sizeof(Point)) << ','
                         << bruteEdges.size() << ','
                         << bruteChecksum << ",1,"
                         << bruteStats.nodesVisited << ','
                         << bruteStats.nodesPruned << ','
                         << bruteStats.boxChecks << ','
                         << bruteStats.distanceChecks << ','
                         << bruteChecksPerSec;

                    appendLine(file, header, line.str());
                }

                {
                    std::ostringstream line;
                    line << seed << ',' << rep << ',' << n << ',' << radius << ",quadtree,"
                         << std::fixed << std::setprecision(6)
                         << qtM.milliseconds << ','
                         << qtBuildMs << ','
                         << qtRunMs << ','
                         << qtM.cacheMisses << ','
                         << qtM.cacheReferences << ','
                         << qtM.rssBeforeBytes << ','
                         << qtM.rssAfterBytes << ','
                         << static_cast<long long>(qtM.rssAfterBytes) - static_cast<long long>(qtM.rssBeforeBytes) << ','
                         << qtBytes << ','
                         << qtEdges.size() << ','
                         << qtChecksum << ','
                         << (matches ? 1 : 0) << ','
                         << qtStats.nodesVisited << ','
                         << qtStats.nodesPruned << ','
                         << qtStats.boxChecks << ','
                         << qtStats.distanceChecks << ','
                         << qtChecksPerSec;

                    appendLine(file, header, line.str());
                }
            }
        }
    }
}

void Benchmark::runColoringBenchmarks() {
    ensureDirectory(cfg.outputDirectory);
    const std::string file = cfg.outputDirectory + "/coloring.csv";
    const std::string header =
            "seed,repetition,n,radius,approach,time_ms,cache_misses,cache_references,rss_before_bytes,rss_after_bytes,"
            "rss_delta_bytes,num_conflict_edges,feasible,colors_used,valid,matches_optimal";

    std::cout << "Running Coloring Benchmark\n";

    for (std::size_t n : cfg.coloring.nodeCounts) {
        for (double radius : cfg.coloring.radii) {

            const unsigned totalRuns = cfg.coloring.warmupRuns + cfg.coloring.repetitions;

            for (unsigned run = 0; run < totalRuns; ++run) {
                const bool isWarmup = run < cfg.coloring.warmupRuns;
                const unsigned rep = isWarmup ? 0 : run - cfg.coloring.warmupRuns;

                const std::uint32_t seed = cfg.coloring.seed + rep;
                auto points = generatePoints(n, cfg.coloring.coordinateMax, seed);
                const std::string json = writeColoringJson(cfg.outputDirectory, points, radius, n, run, seed);

                int optimalColors = -1;
                if (cfg.coloring.runBruteForce) {
                    std::cout << "Run: " << run << " - Number of nodes: " << n << " - Radius: " << radius << " - Brute force\n";

                    Coloring c;
                    size_t num_edges = c.loadFromJson(json);
                    Coloring::Solution sol;
                    Measurement m = measure([&] { sol = c.solveBruteForce(); }, cfg.coloring.cache.enabled);
                    optimalColors = sol.colorsUsed;
                    std::ostringstream line;

                    if (!isWarmup) {
                        line << seed << ',' << rep << ',' << n << ',' << radius << ",brute_force,"
                             << std::fixed << std::setprecision(6) << m.milliseconds << ','
                             << m.cacheMisses << ',' << m.cacheReferences << ','
                             << m.rssBeforeBytes << ',' << m.rssAfterBytes << ','
                             << static_cast<long long>(m.rssAfterBytes) - static_cast<long long>(m.rssBeforeBytes) << ','
                             << num_edges << ','
                             << sol.feasible << ',' << sol.colorsUsed << ',' << c.isValid(sol) << ",1";
                        appendLine(file, header, line.str());
                    }
                }

                Coloring c;
                size_t num_edges = c.loadFromJson(json);
                Coloring::Solution sol;

                std::cout << "Run: " << run << " - Number of nodes: " << n << " - Radius: " << radius << " - Welsh Powell\n";

                Measurement m = measure([&] { sol = c.solveWelshPowell(); }, cfg.coloring.cache.enabled);
                const bool matchesOptimal = optimalColors < 0 || sol.colorsUsed == optimalColors;
                std::ostringstream line;

                if (isWarmup){
                    continue;
                }

                line << seed << ',' << rep << ',' << n << ',' << radius << ",welsh_powell,"
                     << std::fixed << std::setprecision(6) << m.milliseconds << ','
                     << m.cacheMisses << ',' << m.cacheReferences << ','
                     << m.rssBeforeBytes << ',' << m.rssAfterBytes << ','
                     << static_cast<long long>(m.rssAfterBytes) - static_cast<long long>(m.rssBeforeBytes) << ','
                     << num_edges << ','
                     << sol.feasible << ',' << sol.colorsUsed << ',' << c.isValid(sol) << ',' << matchesOptimal;
                appendLine(file, header, line.str());
            }
        }
    }
}

void Benchmark::runShortestPathBenchmarks() {
    ensureDirectory(cfg.outputDirectory);
    const std::string file = cfg.outputDirectory + "/shortest_path.csv";
    const std::string header =
            "seed,repetition,n,average_degree,algorithm,time_ms,cache_misses,cache_references,rss_before_bytes,rss_after_bytes,"
            "rss_delta_bytes,dest_dist,path_checksum,matches_reference";

    std::cout << "Running Shortest Path Benchmarks\n";

    for (std::size_t n : cfg.shortestPath.graphSizes) {
        for (double conn : cfg.shortestPath.averageDegrees) {

            const unsigned totalRuns = cfg.shortestPath.warmupRuns + cfg.shortestPath.repetitions;

            for (unsigned run = 0; run < totalRuns; ++run) {
                const bool isWarmup = run < cfg.shortestPath.warmupRuns;
                const unsigned rep = isWarmup ? 0 : run - cfg.shortestPath.warmupRuns;

                const std::uint32_t seed = cfg.shortestPath.seed + rep;
                double refDist = 0.0;
                std::uint64_t refChecksum = 0;

                for (const std::string& alg : {std::string("dijkstra_fibonacci"), std::string("astar_fibonacci")}) {
                    std::cout << "Run: " << run << " - Number of nodes: " << n << " - average_degree: " << conn << " - " << alg << "\n";
                    Multigraph g = generateGraph(n, conn, seed);
                    auto src = g.getVertex(0);
                    auto dst = g.getVertex((u_int)n - 1);
                    Measurement m = measure([&] {
                        if (alg == "dijkstra_fibonacci") g.dijkstra(src, dst, FIBONACCI_HEAP);
                        else g.astar(src, dst, FIBONACCI_HEAP);
                    }, cfg.shortestPath.cache.enabled);
                    const double dist = pathCostOrDist(g, n - 1);
                    const std::uint64_t checksum = checksumPath(g);
                    bool matches = true;
                    if (alg == "dijkstra_fibonacci") { refDist = dist; refChecksum = checksum; }
                    else { matches = std::abs(dist - refDist) <= 1e-7 && checksum == refChecksum; }

                    if (isWarmup){
                        continue;
                    }

                    std::ostringstream line;
                    line << seed << ',' << rep << ',' << n << ',' << conn << ',' << alg << ','
                         << std::fixed << std::setprecision(6) << m.milliseconds << ','
                         << m.cacheMisses << ',' << m.cacheReferences << ','
                         << m.rssBeforeBytes << ',' << m.rssAfterBytes << ','
                         << static_cast<long long>(m.rssAfterBytes) - static_cast<long long>(m.rssBeforeBytes) << ','
                         << std::setprecision(12) << dist << ',' << checksum << ',' << matches;
                    appendLine(file, header, line.str());
                }
            }
        }
    }
}

void Benchmark::runPrimBenchmarks() {
    ensureDirectory(cfg.outputDirectory);
    const std::string file = cfg.outputDirectory + "/prim.csv";
    const std::string header =
            "seed,repetition,n,average_degree,pq,time_ms,cache_misses,cache_references,rss_before_bytes,rss_after_bytes,"
            "rss_delta_bytes,visited_vertices,selected_edges,matches_reference";

    std::cout << "Running Prim Benchmarks\n";

    for (std::size_t n : cfg.prim.graphSizes) {
        for (double conn : cfg.prim.averageDegrees) {

            const unsigned totalRuns = cfg.prim.warmupRuns + cfg.prim.repetitions;

            for (unsigned run = 0; run < totalRuns; ++run) {
                const bool isWarmup = run < cfg.prim.warmupRuns;
                const unsigned rep = isWarmup ? 0 : run - cfg.prim.warmupRuns;

                const std::uint32_t seed = cfg.prim.seed + rep;
                std::uint64_t refEdges = 0;
                std::size_t refVisited = 0;
                for (PriorityQueueSelected pq : {FIBONACCI_HEAP, MUTABLE_PRIORITY_QUEUE}) {
                    std::cout << "Run: " << run << " - Number of nodes: " << n << " - average_degree: " << conn << " - " << pqName(pq) << "\n";
                    Multigraph g = generateGraph(n, conn, seed);
                    std::vector<std::shared_ptr<Vertex>> mst;
                    Measurement m = measure([&] { mst = g.prim(g.getVertex(0), pq); }, cfg.prim.cache.enabled);
                    std::uint64_t selectedEdges = mstSelectedEdgeCount(g);
                    bool matches = true;
                    if (pq == FIBONACCI_HEAP) { refEdges = selectedEdges; refVisited = mst.size(); }
                    else { matches = selectedEdges == refEdges && mst.size() == refVisited; }

                    if (isWarmup){
                        continue;
                    }

                    std::ostringstream line;
                    line << seed << ',' << rep << ',' << n << ',' << conn << ',' << pqName(pq) << ','
                         << std::fixed << std::setprecision(6) << m.milliseconds << ','
                         << m.cacheMisses << ',' << m.cacheReferences << ','
                         << m.rssBeforeBytes << ',' << m.rssAfterBytes << ','
                         << static_cast<long long>(m.rssAfterBytes) - static_cast<long long>(m.rssBeforeBytes) << ','
                         << mst.size() << ',' << selectedEdges << ',' << matches;
                    appendLine(file, header, line.str());
                }
            }
        }
    }
}

std::string Benchmark::pqName(PriorityQueueSelected pqs) {
    switch (pqs) {
        case BRUTE_FORCE: return "brute_force";
        case MUTABLE_PRIORITY_QUEUE: return "mutable_binary_heap";
        case FIBONACCI_HEAP: return "fibonacci_heap";
    }
    return "unknown";
}

std::string Benchmark::csvEscape(const std::string& s) {
    if (s.find_first_of(",\"") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) out += (c == '"' ? "\"\"" : std::string(1, c));
    out += "\"";
    return out;
}

void Benchmark::ensureDirectory(const std::string& path) {
    std::filesystem::create_directories(path);
}

void Benchmark::appendLine(const std::string& path, const std::string& header, const std::string& line) {
    const bool exists = std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) throw std::runtime_error("Benchmark: cannot write " + path);
    if (!exists) out << header << '\n';
    out << line << '\n';
    out.flush();
}

Benchmark::Measurement Benchmark::measure(const std::function<void()>& fn, bool collectCacheMisses) {
    Measurement m;
    m.rssBeforeBytes = currentRSSBytes();

#if defined(ENABLE_BENCHMARK_CACHE) && defined(__linux__)
    int fdMiss = -1;
    int fdRef = -1;
    if (collectCacheMisses) {
        perf_event_attr pe{};
        pe.type = PERF_TYPE_HARDWARE;
        pe.size = sizeof(perf_event_attr);
        pe.config = PERF_COUNT_HW_CACHE_MISSES;
        pe.disabled = 1;
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;
        fdMiss = static_cast<int>(syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0));

        perf_event_attr pr{};
        pr.type = PERF_TYPE_HARDWARE;
        pr.size = sizeof(perf_event_attr);
        pr.config = PERF_COUNT_HW_CACHE_REFERENCES;
        pr.disabled = 1;
        pr.exclude_kernel = 1;
        pr.exclude_hv = 1;
        fdRef = static_cast<int>(syscall(__NR_perf_event_open, &pr, 0, -1, -1, 0));

        if (fdMiss >= 0) { ioctl(fdMiss, PERF_EVENT_IOC_RESET, 0); ioctl(fdMiss, PERF_EVENT_IOC_ENABLE, 0); }
        if (fdRef >= 0) { ioctl(fdRef, PERF_EVENT_IOC_RESET, 0); ioctl(fdRef, PERF_EVENT_IOC_ENABLE, 0); }
    }
#else
    (void)collectCacheMisses;
#endif

    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    m.milliseconds = std::chrono::duration<double, std::milli>(end - start).count();

#if defined(ENABLE_BENCHMARK_CACHE) && defined(__linux__)
    if (fdMiss >= 0) {
        ioctl(fdMiss, PERF_EVENT_IOC_DISABLE, 0);
        long long value = 0;
        if (read(fdMiss, &value, sizeof(value)) == sizeof(value)) m.cacheMisses = value;
        close(fdMiss);
    }
    if (fdRef >= 0) {
        ioctl(fdRef, PERF_EVENT_IOC_DISABLE, 0);
        long long value = 0;
        if (read(fdRef, &value, sizeof(value)) == sizeof(value)) m.cacheReferences = value;
        close(fdRef);
    }
#endif

    m.rssAfterBytes = currentRSSBytes();
    return m;
}

std::size_t Benchmark::currentRSSBytes() {
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    long pagesTotal = 0;
    long pagesResident = 0;
    statm >> pagesTotal >> pagesResident;
    const long pageSize = sysconf(_SC_PAGESIZE);
    return static_cast<std::size_t>(std::max<long>(0, pagesResident) * pageSize);
#else
    return 0;
#endif
}

Multigraph Benchmark::generateGraph(std::size_t n, unsigned averageDegree, std::uint32_t seed) {
    if (n < 2) {
        throw std::invalid_argument("Benchmark graph must have at least 2 vertices");
    }

    if (averageDegree < 2) {
        averageDegree = 2;
    }

    std::mt19937 rng(seed);

    std::uniform_real_distribution<double> coordX(526000.0, 536000.0);
    std::uniform_real_distribution<double> coordY(4554000.0, 4560000.0);
    std::uniform_int_distribution<int> modeD(0, 2);
    std::uniform_int_distribution<std::size_t> vtxD(0, n - 1);

    static const double SPEEDS[] = {
            SPEED_WALK,
            SPEED_BUS,
            SPEED_METRO
    };

    Multigraph g;

    std::vector<std::pair<double, double>> coords(n);

    for (std::size_t i = 0; i < n; ++i) {
        coords[i] = {coordX(rng), coordY(rng)};
        g.addVertex(coords[i].first, coords[i].second, "v" + std::to_string(i));
    }

    auto makeWeight = [&](std::size_t a, std::size_t b, Mode mode) -> double {
        const double dx = coords[a].first - coords[b].first;
        const double dy = coords[a].second - coords[b].second;
        const double dist = std::sqrt(dx * dx + dy * dy);

        return dist / SPEEDS[static_cast<int>(mode)];
    };

    auto edgeKey = [](std::size_t a, std::size_t b) -> std::uint64_t {
        if (a > b) {
            std::swap(a, b);
        }

        return (static_cast<std::uint64_t>(a) << 32)
               ^ static_cast<std::uint64_t>(b);
    };

    std::unordered_set<std::uint64_t> seen;
    seen.reserve(n * static_cast<std::size_t>(averageDegree));

    // Random spanning tree first, guaranteeing connectivity.
    for (std::size_t i = 1; i < n; ++i) {
        std::uniform_int_distribution<std::size_t> parentD(0, i - 1);

        const std::size_t j = parentD(rng);
        const Mode mode = static_cast<Mode>(modeD(rng));

        seen.insert(edgeKey(i, j));
        g.addEdge(
                static_cast<u_int>(i),
                static_cast<u_int>(j),
                makeWeight(i, j, mode),
                mode
        );
    }

    const std::size_t desiredEdges =
            std::max<std::size_t>(n - 1, (n * static_cast<std::size_t>(averageDegree)) / 2);

    const std::size_t extraTarget = desiredEdges - (n - 1);

    std::size_t attempts = 0;
    const std::size_t maxAttempts = extraTarget * 10 + 1000;

    while (seen.size() < desiredEdges && attempts < maxAttempts) {
        ++attempts;

        std::size_t a = vtxD(rng);
        std::size_t b = vtxD(rng);

        if (a == b) {
            continue;
        }

        const std::uint64_t key = edgeKey(a, b);

        if (seen.find(key) != seen.end()) {
            continue;
        }

        seen.insert(key);

        const Mode mode = static_cast<Mode>(modeD(rng));

        g.addEdge(
                static_cast<u_int>(a),
                static_cast<u_int>(b),
                makeWeight(a, b, mode),
                mode
        );
    }

    return g;
}

std::vector<Benchmark::Point> Benchmark::generatePoints(std::size_t n, double coordinateMax, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> coord(0.0, coordinateMax);
    std::vector<Point> points;
    points.reserve(n);
    for (std::size_t i = 0; i < n; ++i) points.push_back({i, coord(rng), coord(rng)});
    return points;
}

std::string Benchmark::writeColoringJson(const std::string& dir, const std::vector<Point>& points, double radius,
                                         std::size_t n, unsigned repetition, std::uint32_t seed) {
    const std::string path = dir + "/coloring_input_n" + std::to_string(n) + "_r" + std::to_string((int)radius)
                             + "_rep" + std::to_string(repetition) + "_seed" + std::to_string(seed) + ".json";
    std::ofstream out(path);
    if (!out.is_open()) throw std::runtime_error("Benchmark: cannot write " + path);
    out << "{\n  \"interference_radius\": " << std::setprecision(12) << radius << ",\n  \"antennas\": [\n";
    for (std::size_t i = 0; i < points.size(); ++i) {
        out << "    {\"id\": \"ap_" << points[i].id << "\", \"x\": " << points[i].x << ", \"y\": " << points[i].y << "}";
        if (i + 1 != points.size()) out << ',';
        out << '\n';
    }
    out << "  ]\n}\n";
    return path;
}

double Benchmark::pathCostOrDist(const Multigraph& graph, std::size_t destId) {
    const auto dst = graph.getVertex((u_int)destId);
    return dst ? dst->getDist() : DBL_MAX;
}

std::uint64_t Benchmark::mstSelectedEdgeCount(const Multigraph& graph) {
    std::uint64_t count = 0;
    for (const auto& v : graph.getVertexSet()) {
        for (const auto& e : v->getAdj()) if (e->isSelected()) ++count;
    }
    return count;
}

std::uint64_t Benchmark::checksumPath(const Multigraph& graph) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto& v : graph.getVertexSet()) {
        auto p = v->getPath();
        if (!p) continue;
        h ^= static_cast<std::uint64_t>(v->getId() + 0x9e3779b97f4a7c15ULL);
        h *= 1099511628211ULL;
        h ^= static_cast<std::uint64_t>(p->getOrigin()->getId() + 0xbf58476d1ce4e5b9ULL);
        h *= 1099511628211ULL;
    }
    return h;
}

std::vector<std::pair<std::size_t, std::size_t>> Benchmark::buildInterferenceBruteForce(
        const std::vector<Point>& points, double radius, QuadtreeStats& stats) {
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    const double r2 = radius * radius;
    for (std::size_t i = 0; i < points.size(); ++i) {
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            ++stats.distanceChecks;
            const double dx = points[i].x - points[j].x;
            const double dy = points[i].y - points[j].y;
            if (dx * dx + dy * dy <= r2) edges.emplace_back(i, j);
        }
    }
    return edges;
}

std::vector<std::pair<std::size_t, std::size_t>> Benchmark::buildInterferenceQuadtree(
        const std::vector<Point>& points, double radius, QuadtreeStats& stats,
        double& buildMs, std::size_t& estimatedQtBytes) {
    std::vector<std::shared_ptr<Vertex>> verts;
    verts.reserve(points.size());
    for (const Point& p : points) verts.emplace_back(std::make_shared<Vertex>((u_int)p.id, p.x, p.y, "ap_" + std::to_string(p.id)));

    const auto buildStart = std::chrono::steady_clock::now();
    Quadtree qt(verts);
    const auto buildEnd = std::chrono::steady_clock::now();
    buildMs = std::chrono::duration<double, std::milli>(buildEnd - buildStart).count();

    estimatedQtBytes = verts.size() * (sizeof(Vertex) + sizeof(std::shared_ptr<Vertex>) + sizeof(Vertex*));

    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (const Point& p : points) {
        auto nearby = qt.rangeSearch(p.x, p.y, radius, stats);
        for (Vertex* v : nearby) {
            std::size_t j = v->getId();
            if (j > p.id) edges.emplace_back(p.id, j);
        }
    }
    std::sort(edges.begin(), edges.end());
    return edges;
}

std::uint64_t Benchmark::edgeChecksum(const std::vector<std::pair<std::size_t, std::size_t>>& edges) {
    std::uint64_t h = 1469598103934665603ULL;
    for (auto [a, b] : edges) {
        h ^= static_cast<std::uint64_t>(a + 0x9e3779b97f4a7c15ULL);
        h *= 1099511628211ULL;
        h ^= static_cast<std::uint64_t>(b + 0xbf58476d1ce4e5b9ULL);
        h *= 1099511628211ULL;
    }
    return h;
}
