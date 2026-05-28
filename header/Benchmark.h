#ifndef EDAA_BENCHMARK_H
#define EDAA_BENCHMARK_H

#include "Multigraph.h"
#include "Coloring.h"
#include "Quadtree.h"
#include "PriorityQueue.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <vector>

class Benchmark {
public:
    struct CacheConfig {
        bool enabled = true;
    };

    struct PriorityQueueConfig {
        std::vector<std::size_t> graphSizes = {100, 500, 1000};
        std::vector<double> connectivities = {0.01, 0.05, 0.10};
        unsigned repetitions = 3;
        unsigned warmupRuns = 1;
        std::uint32_t seed = 12345;
        CacheConfig cache{};
    };

    struct InterferenceGraphConfig {
        std::vector<std::size_t> nodeCounts = {1000, 5000, 10000};
        std::vector<double> radii = {25.0, 50.0, 100.0};
        double coordinateMax = 10000.0;
        unsigned repetitions = 3;
        unsigned warmupRuns = 1;
        std::uint32_t seed = 12345;
        CacheConfig cache{};
    };

    struct ColoringConfig {
        std::vector<std::size_t> nodeCounts = {8, 10, 12};
        std::vector<double> radii = {2000.0, 3500.0};
        double coordinateMax = 10000.0;
        unsigned repetitions = 3;
        unsigned warmupRuns = 1;
        std::uint32_t seed = 12345;
        bool runBruteForce = true;
        CacheConfig cache{};
    };

    struct ShortestPathConfig {
        std::vector<std::size_t> graphSizes = {100, 500, 1000};
        std::vector<double> connectivities = {0.01, 0.05, 0.10};
        unsigned repetitions = 3;
        unsigned warmupRuns = 1;
        std::uint32_t seed = 12345;
        CacheConfig cache{};
    };

    struct PrimConfig {
        std::vector<std::size_t> graphSizes = {100, 500, 1000};
        std::vector<double> connectivities = {0.01, 0.05, 0.10};
        unsigned repetitions = 3;
        unsigned warmupRuns = 1;
        std::uint32_t seed = 12345;
        CacheConfig cache{};
    };

    struct Config {
        std::string outputDirectory = "../benchmark";
        PriorityQueueConfig priorityQueues{};
        InterferenceGraphConfig interference{};
        ColoringConfig coloring{};
        ShortestPathConfig shortestPath{};
        PrimConfig prim{};

        bool runPriorityQueues = true;
        bool runInterferenceGraph = true;
        bool runColoring = true;
        bool runShortestPath = true;
        bool runPrim = true;
    };

    Benchmark();
    explicit Benchmark(Config config);

    void run();
    void runPriorityQueueBenchmarks();
    void runInterferenceGraphBenchmarks();
    void runColoringBenchmarks();
    void runShortestPathBenchmarks();
    void runPrimBenchmarks();

private:
    struct Measurement {
        double milliseconds = 0.0;
        long long cacheMisses = -1;
        long long cacheReferences = -1;
        std::size_t rssBeforeBytes = 0;
        std::size_t rssAfterBytes = 0;
    };

    struct Point {
        std::size_t id = 0;
        double x = 0.0;
        double y = 0.0;
    };

    Config cfg;

    static std::string pqName(PriorityQueueSelected pqs);
    static std::string csvEscape(const std::string& s);
    static void ensureDirectory(const std::string& path);
    static void appendLine(const std::string& path, const std::string& header, const std::string& line);
    static Measurement measure(const std::function<void()>& fn, bool collectCacheMisses);
    static std::size_t currentRSSBytes();

    static Multigraph generateGraph(std::size_t n, double connectivity, std::uint32_t seed);
    static std::vector<Point> generatePoints(std::size_t n, double coordinateMax, std::uint32_t seed);
    static std::string writeColoringJson(const std::string& dir, const std::vector<Point>& points, double radius,
                                         std::size_t n, unsigned repetition, std::uint32_t seed);

    static double pathCostOrDist(const Multigraph& graph, std::size_t destId);
    static std::uint64_t mstSelectedEdgeCount(const Multigraph& graph);
    static std::uint64_t checksumPath(const Multigraph& graph);

    static std::vector<std::pair<std::size_t, std::size_t>> buildInterferenceBruteForce(
            const std::vector<Point>& points, double radius, QuadtreeStats& stats);

    static std::vector<std::pair<std::size_t, std::size_t>> buildInterferenceQuadtree(
            const std::vector<Point>& points, double radius, QuadtreeStats& stats,
            double& buildMs, std::size_t& estimatedQtBytes);

    static std::uint64_t edgeChecksum(const std::vector<std::pair<std::size_t, std::size_t>>& edges);
};

#endif // EDAA_BENCHMARK_H
