#include "../header/BenchmarkHardware.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>

#if HARDWARE_COUNTERS_AVAILABLE
#include <linux/perf_event.h>
    #include <sys/ioctl.h>
    #include <sys/syscall.h>
    #include <unistd.h>
    #include <cstring>
#endif

#if HARDWARE_COUNTERS_AVAILABLE

int HardwareCounterGroup::openCounter(unsigned int type, unsigned long long config) {
    perf_event_attr pe{};
    std::memset(&pe, 0, sizeof(pe));

    pe.type = type;
    pe.size = sizeof(pe);
    pe.config = config;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    return static_cast<int>(
            syscall(
                    SYS_perf_event_open,
                    &pe,
                    0,      // current thread/process
                    -1,     // any CPU
                    -1,     // no group leader
                    0
            )
    );
}

long long HardwareCounterGroup::readCounter(int fd) {
    if (fd < 0) return 0;

    long long value = 0;
    ssize_t res = read(fd, &value, sizeof(value));

    if (res != sizeof(value)) {
        return 0;
    }

    return value;
}

void HardwareCounterGroup::resetCounter(int fd) {
    if (fd >= 0) {
        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    }
}

void HardwareCounterGroup::enableCounter(int fd) {
    if (fd >= 0) {
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    }
}

void HardwareCounterGroup::disableCounter(int fd) {
    if (fd >= 0) {
        ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    }
}

#endif

HardwareCounterGroup::HardwareCounterGroup() {
#if HARDWARE_COUNTERS_AVAILABLE
    fdCacheRefs = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES);
    fdCacheMisses = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);
    fdInstructions = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
    fdCycles = openCounter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);

    supported =
            fdCacheRefs >= 0 &&
            fdCacheMisses >= 0 &&
            fdInstructions >= 0 &&
            fdCycles >= 0;
#else
    supported = false;
#endif
}

HardwareCounterGroup::~HardwareCounterGroup() {
#if HARDWARE_COUNTERS_AVAILABLE
    if (fdCacheRefs >= 0) close(fdCacheRefs);
    if (fdCacheMisses >= 0) close(fdCacheMisses);
    if (fdInstructions >= 0) close(fdInstructions);
    if (fdCycles >= 0) close(fdCycles);
#endif
}

bool HardwareCounterGroup::isSupported() const {
    return supported;
}

void HardwareCounterGroup::start() {
#if HARDWARE_COUNTERS_AVAILABLE
    if (!supported) return;

    resetCounter(fdCacheRefs);
    resetCounter(fdCacheMisses);
    resetCounter(fdInstructions);
    resetCounter(fdCycles);

    enableCounter(fdCacheRefs);
    enableCounter(fdCacheMisses);
    enableCounter(fdInstructions);
    enableCounter(fdCycles);
#endif
}

HardwareCounters HardwareCounterGroup::stop() {
    HardwareCounters counters;

#if HARDWARE_COUNTERS_AVAILABLE
    if (!supported) return counters;

    disableCounter(fdCacheRefs);
    disableCounter(fdCacheMisses);
    disableCounter(fdInstructions);
    disableCounter(fdCycles);

    counters.cacheReferences = readCounter(fdCacheRefs);
    counters.cacheMisses = readCounter(fdCacheMisses);
    counters.instructions = readCounter(fdInstructions);
    counters.cycles = readCounter(fdCycles);
#endif

    return counters;
}

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkHardware
// ─────────────────────────────────────────────────────────────────────────────

BenchmarkHardware::BenchmarkHardware(Benchmark& benchmark)
        : benchmark(benchmark) {}

double BenchmarkHardware::euclideanDistance(const std::shared_ptr<Vertex>& a,
                                            const std::shared_ptr<Vertex>& b) {
    double dx = a->getCoordinates().getX() - b->getCoordinates().getX();
    double dy = a->getCoordinates().getY() - b->getCoordinates().getY();

    return std::sqrt(dx * dx + dy * dy);
}

double BenchmarkHardware::mstWeight(const std::vector<std::shared_ptr<Vertex>>& mst) {
    double total = 0.0;

    for (const auto& v : mst) {
        if (v->getPath() != nullptr) {
            total += v->getDist();
        }
    }

    return total;
}

Multigraph BenchmarkHardware::generateConnectedGraph(int n,
                                                     int avgDegree,
                                                     unsigned int seed,
                                                     double minX,
                                                     double maxX,
                                                     double minY,
                                                     double maxY) {
    Multigraph g;

    std::mt19937 rng(seed);

    std::uniform_real_distribution<double> rx(minX, maxX);
    std::uniform_real_distribution<double> ry(minY, maxY);

    std::vector<u_int> ids;
    ids.reserve(n);

    for (int i = 0; i < n; i++) {
        ids.push_back(g.addVertex(
                rx(rng),
                ry(rng),
                "v_" + std::to_string(i)
        ));
    }

    const auto& verts = g.getVertexSet();

    std::set<std::pair<u_int, u_int>> usedEdges;

    auto addUndirectedEdge = [&](u_int a, u_int b) {
        if (a == b) return false;

        u_int x = std::min(a, b);
        u_int y = std::max(a, b);

        if (usedEdges.count({x, y})) {
            return false;
        }

        usedEdges.insert({x, y});

        double w = euclideanDistance(verts[a], verts[b]);

        // If your graph stores undirected edges differently, change this.
        g.addEdge(a, b, w, WALK);
        g.addEdge(b, a, w, WALK);

        return true;
    };

    // Guarantee connectivity using a chain.
    for (int i = 0; i + 1 < n; i++) {
        addUndirectedEdge(i, i + 1);
    }

    long long targetUndirectedEdges =
            std::max<long long>(n - 1, static_cast<long long>(n) * avgDegree / 2);

    std::uniform_int_distribution<int> vertexDist(0, n - 1);

    while (static_cast<long long>(usedEdges.size()) < targetUndirectedEdges) {
        u_int a = vertexDist(rng);
        u_int b = vertexDist(rng);

        addUndirectedEdge(a, b);
    }

    return g;
}

void BenchmarkHardware::recordCacheMetric(const std::string& benchmarkName,
                                          const std::string& variant,
                                          int n,
                                          double paramSecondary,
                                          double elapsed,
                                          const HardwareCounters& counters,
                                          const std::string& metricName,
                                          double metricValue,
                                          bool dnf,
                                          const std::string& notes) {
    BenchmarkRow row;

    row.benchmark = benchmarkName;
    row.variant = variant;
    row.paramN = n;
    row.paramP = paramSecondary;
    row.elapsedS = elapsed;
    row.quality = metricValue;
    row.qualityLabel = metricName;
    row.dnf = dnf;

    row.notes =
            notes +
            " cache_references=" + std::to_string(counters.cacheReferences) +
            " cache_misses=" + std::to_string(counters.cacheMisses) +
            " cache_miss_rate=" + std::to_string(counters.missRate()) +
            " instructions=" + std::to_string(counters.instructions) +
            " cycles=" + std::to_string(counters.cycles) +
            " ipc=" + std::to_string(counters.instructionsPerCycle());

    benchmark.record(row);
}

// ─────────────────────────────────────────────────────────────────────────────
// Prim hardware benchmark
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkHardware::runPrimCache() {
    std::cout << "\n== Hardware Counters — Prim: FibonacciHeap vs MutablePriorityQueue ==\n";

#ifndef __linux__
    std::cout << "  Hardware counters via perf_event_open are Linux-only.\n";
    std::cout << "  This machine appears to be macOS/Apple Silicon.\n";
    std::cout << "  Use Instruments/xctrace for cache and CPU profiling on M1 Pro.\n";
    std::cout << "  For benchmark CSVs on macOS, use elapsed time, heap-operation counts, and operations/sec instead.\n";
    return;
#endif

    HardwareCounterGroup testCounters;

    if (!testCounters.isSupported()) {
        std::cout << "  Hardware counters are not available.\n";
        std::cout << "  Linux fix: sudo sysctl kernel.perf_event_paranoid=1\n";
        std::cout << "  Docker fix: run with --cap-add=PERFMON or --privileged\n";
        return;
    }

    const std::vector<int> nodeCounts = {
            1000, 2000, 5000, 10000
    };

    const std::vector<int> avgDegrees = {
            4, 8, 16, 32
    };

    for (int avgDegree : avgDegrees) {
        std::cout << "  avg_degree=" << avgDegree << "\n";

        for (int n : nodeCounts) {
            Multigraph g = generateConnectedGraph(
                    n,
                    avgDegree,
                    benchmark.cfg.pqRngSeed + n + avgDegree,
                    benchmark.cfg.coordMinX,
                    benchmark.cfg.coordMaxX,
                    benchmark.cfg.coordMinY,
                    benchmark.cfg.coordMaxY
            );

            auto src = g.getVertex(0);

            // ── Prim FibonacciHeap ───────────────────────────────────────
            HardwareCounters fibCounters;
            double fibWeight = 0.0;
            bool fibDnf = false;

            double elapsedFib = benchmark.timed([&] {
                HardwareCounterGroup counters;
                counters.start();

                auto mst = g.prim(src, FIBONACCI_HEAP);

                fibCounters = counters.stop();

                fibWeight = mstWeight(mst);
                fibDnf = static_cast<int>(mst.size()) != n;
            });

            std::string fibNotes =
                    "avg_degree=" + std::to_string(avgDegree) +
                    " mst_weight=" + std::to_string(fibWeight);

            recordCacheMetric("prim_cache", "Prim_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "cache_misses",
                              static_cast<double>(fibCounters.cacheMisses),
                              fibDnf, fibNotes);

            recordCacheMetric("prim_cache", "Prim_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "cache_references",
                              static_cast<double>(fibCounters.cacheReferences),
                              fibDnf, fibNotes);

            recordCacheMetric("prim_cache", "Prim_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "cache_miss_rate",
                              fibCounters.missRate(),
                              fibDnf, fibNotes);

            recordCacheMetric("prim_cache", "Prim_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "instructions",
                              static_cast<double>(fibCounters.instructions),
                              fibDnf, fibNotes);

            recordCacheMetric("prim_cache", "Prim_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "cycles",
                              static_cast<double>(fibCounters.cycles),
                              fibDnf, fibNotes);

            recordCacheMetric("prim_cache", "Prim_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "ipc",
                              fibCounters.instructionsPerCycle(),
                              fibDnf, fibNotes);

            // ── Prim MutablePriorityQueue ────────────────────────────────
            HardwareCounters mutableCounters;
            double mutableWeight = 0.0;
            bool mutableDnf = false;

            double elapsedMutable = benchmark.timed([&] {
                HardwareCounterGroup counters;
                counters.start();

                auto mst = g.prim(src, MUTABLE_PRIORITY_QUEUE);

                mutableCounters = counters.stop();

                mutableWeight = mstWeight(mst);
                mutableDnf = static_cast<int>(mst.size()) != n;
            });

            std::string mutableNotes =
                    "avg_degree=" + std::to_string(avgDegree) +
                    " mst_weight=" + std::to_string(mutableWeight);

            recordCacheMetric("prim_cache", "Prim_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "cache_misses",
                              static_cast<double>(mutableCounters.cacheMisses),
                              mutableDnf, mutableNotes);

            recordCacheMetric("prim_cache", "Prim_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "cache_references",
                              static_cast<double>(mutableCounters.cacheReferences),
                              mutableDnf, mutableNotes);

            recordCacheMetric("prim_cache", "Prim_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "cache_miss_rate",
                              mutableCounters.missRate(),
                              mutableDnf, mutableNotes);

            recordCacheMetric("prim_cache", "Prim_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "instructions",
                              static_cast<double>(mutableCounters.instructions),
                              mutableDnf, mutableNotes);

            recordCacheMetric("prim_cache", "Prim_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "cycles",
                              static_cast<double>(mutableCounters.cycles),
                              mutableDnf, mutableNotes);

            recordCacheMetric("prim_cache", "Prim_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "ipc",
                              mutableCounters.instructionsPerCycle(),
                              mutableDnf, mutableNotes);

            std::cout << "    N=" << n
                      << "  Fib=" << std::fixed << std::setprecision(5) << elapsedFib << "s"
                      << "  Mutable=" << elapsedMutable << "s"
                      << "  Fib_miss_rate=" << fibCounters.missRate()
                      << "  Mutable_miss_rate=" << mutableCounters.missRate()
                      << "  Fib_misses=" << fibCounters.cacheMisses
                      << "  Mutable_misses=" << mutableCounters.cacheMisses;

            if (std::abs(fibWeight - mutableWeight) > 1e-6) {
                std::cout << "  [MST MISMATCH]";
            }

            std::cout << "\n";
        }
    }

    std::cout << "  Done.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Dijkstra hardware benchmark
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkHardware::runDijkstraCache() {
    std::cout << "\n== Hardware Counters — Dijkstra: FibonacciHeap vs MutablePriorityQueue ==\n";

    HardwareCounterGroup testCounters;

    if (!testCounters.isSupported()) {
        std::cout << "  Hardware counters are not available.\n";
        std::cout << "  Linux fix: sudo sysctl kernel.perf_event_paranoid=1\n";
        std::cout << "  Docker fix: run with --cap-add=PERFMON or --privileged\n";
        return;
    }

    const std::vector<int> nodeCounts = {
            1000, 2000, 5000, 10000
    };

    const std::vector<int> avgDegrees = {
            4, 8, 16, 32
    };

    for (int avgDegree : avgDegrees) {
        std::cout << "  avg_degree=" << avgDegree << "\n";

        for (int n : nodeCounts) {
            Multigraph g = generateConnectedGraph(
                    n,
                    avgDegree,
                    benchmark.cfg.pqRngSeed + 911 + n + avgDegree,
                    benchmark.cfg.coordMinX,
                    benchmark.cfg.coordMaxX,
                    benchmark.cfg.coordMinY,
                    benchmark.cfg.coordMaxY
            );

            auto src = g.getVertex(0);
            auto dest = g.getVertex(n - 1);

            // ── Dijkstra FibonacciHeap ───────────────────────────────────
            HardwareCounters fibCounters;
            bool fibDnf = false;
            int fibPathSize = 0;

            double elapsedFib = benchmark.timed([&] {
                HardwareCounterGroup counters;
                counters.start();

                auto path = g.dijkstra(src, dest, FIBONACCI_HEAP);

                fibCounters = counters.stop();

                fibPathSize = static_cast<int>(path.size());
                fibDnf = path.empty();
            });

            std::string fibNotes =
                    "avg_degree=" + std::to_string(avgDegree) +
                    " path_size=" + std::to_string(fibPathSize);

            recordCacheMetric("dijkstra_cache", "Dijkstra_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "cache_misses",
                              static_cast<double>(fibCounters.cacheMisses),
                              fibDnf, fibNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "cache_references",
                              static_cast<double>(fibCounters.cacheReferences),
                              fibDnf, fibNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "cache_miss_rate",
                              fibCounters.missRate(),
                              fibDnf, fibNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "instructions",
                              static_cast<double>(fibCounters.instructions),
                              fibDnf, fibNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "cycles",
                              static_cast<double>(fibCounters.cycles),
                              fibDnf, fibNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_FibHeap", n, avgDegree,
                              elapsedFib, fibCounters, "ipc",
                              fibCounters.instructionsPerCycle(),
                              fibDnf, fibNotes);

            // ── Dijkstra MutablePriorityQueue ────────────────────────────
            HardwareCounters mutableCounters;
            bool mutableDnf = false;
            int mutablePathSize = 0;

            double elapsedMutable = benchmark.timed([&] {
                HardwareCounterGroup counters;
                counters.start();

                auto path = g.dijkstra(src, dest, MUTABLE_PRIORITY_QUEUE);

                mutableCounters = counters.stop();

                mutablePathSize = static_cast<int>(path.size());
                mutableDnf = path.empty();
            });

            std::string mutableNotes =
                    "avg_degree=" + std::to_string(avgDegree) +
                    " path_size=" + std::to_string(mutablePathSize);

            recordCacheMetric("dijkstra_cache", "Dijkstra_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "cache_misses",
                              static_cast<double>(mutableCounters.cacheMisses),
                              mutableDnf, mutableNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "cache_references",
                              static_cast<double>(mutableCounters.cacheReferences),
                              mutableDnf, mutableNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "cache_miss_rate",
                              mutableCounters.missRate(),
                              mutableDnf, mutableNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "instructions",
                              static_cast<double>(mutableCounters.instructions),
                              mutableDnf, mutableNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "cycles",
                              static_cast<double>(mutableCounters.cycles),
                              mutableDnf, mutableNotes);

            recordCacheMetric("dijkstra_cache", "Dijkstra_MutablePQ", n, avgDegree,
                              elapsedMutable, mutableCounters, "ipc",
                              mutableCounters.instructionsPerCycle(),
                              mutableDnf, mutableNotes);

            std::cout << "    N=" << n
                      << "  Fib=" << std::fixed << std::setprecision(5) << elapsedFib << "s"
                      << "  Mutable=" << elapsedMutable << "s"
                      << "  Fib_miss_rate=" << fibCounters.missRate()
                      << "  Mutable_miss_rate=" << mutableCounters.missRate()
                      << "  Fib_misses=" << fibCounters.cacheMisses
                      << "  Mutable_misses=" << mutableCounters.cacheMisses
                      << "\n";
        }
    }

    std::cout << "  Done.\n";
}

void BenchmarkHardware::runAll() {
    runPrimCache();
    runDijkstraCache();
}