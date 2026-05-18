#ifndef EDAA_BENCHMARK_HARDWARE_H
#define EDAA_BENCHMARK_HARDWARE_H

#include "Benchmark.h"
#include "Multigraph.h"

#include <string>
#include <vector>
#include <memory>

#ifdef __linux__
#define HARDWARE_COUNTERS_AVAILABLE 1
#else
#define HARDWARE_COUNTERS_AVAILABLE 0
#endif

struct HardwareCounters {
    long long cacheReferences = 0;
    long long cacheMisses = 0;
    long long instructions = 0;
    long long cycles = 0;

    [[nodiscard]] double missRate() const {
        if (cacheReferences == 0) return 0.0;
        return static_cast<double>(cacheMisses) / static_cast<double>(cacheReferences);
    }

    [[nodiscard]] double instructionsPerCycle() const {
        if (cycles == 0) return 0.0;
        return static_cast<double>(instructions) / static_cast<double>(cycles);
    }
};

class HardwareCounterGroup {
private:
#if HARDWARE_COUNTERS_AVAILABLE
    int fdCacheRefs = -1;
    int fdCacheMisses = -1;
    int fdInstructions = -1;
    int fdCycles = -1;
#endif

    bool supported = false;

#if HARDWARE_COUNTERS_AVAILABLE
    static int openCounter(unsigned int type, unsigned long long config);
    static long long readCounter(int fd);
    static void resetCounter(int fd);
    static void enableCounter(int fd);
    static void disableCounter(int fd);
#endif

public:
    HardwareCounterGroup();
    ~HardwareCounterGroup();

    [[nodiscard]] bool isSupported() const;

    void start();
    HardwareCounters stop();
};

class BenchmarkHardware {
private:
    Benchmark& benchmark;

    static double euclideanDistance(const std::shared_ptr<Vertex>& a,
                                    const std::shared_ptr<Vertex>& b);

    static double mstWeight(const std::vector<std::shared_ptr<Vertex>>& mst);

    static Multigraph generateConnectedGraph(int n,
                                             int avgDegree,
                                             unsigned int seed,
                                             double minX,
                                             double maxX,
                                             double minY,
                                             double maxY);

    void recordCacheMetric(const std::string& benchmarkName,
                           const std::string& variant,
                           int n,
                           double paramSecondary,
                           double elapsed,
                           const HardwareCounters& counters,
                           const std::string& metricName,
                           double metricValue,
                           bool dnf,
                           const std::string& notes);

public:
    explicit BenchmarkHardware(Benchmark& benchmark);

    void runPrimCache();
    void runDijkstraCache();
    void runAll();
};

#endif // EDAA_BENCHMARK_HARDWARE_H