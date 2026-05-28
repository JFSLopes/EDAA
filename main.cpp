#include "header/Benchmark.h"
#include "header/BenchmarkHardware.h"

int main() {
    Benchmark::Config cfg;

    cfg.outputDirectory = "../benchmark";

    cfg.priorityQueues.graphSizes = {100, 500, 1000};
    cfg.priorityQueues.connectivities = {0.01, 0.05, 0.10};
    cfg.priorityQueues.repetitions = 3;
    cfg.priorityQueues.seed = 42;
    cfg.priorityQueues.cache.enabled = false;

    cfg.interference.nodeCounts = {1000, 5000};
    cfg.interference.radii = {50.0, 100.0, 200.0};

    cfg.coloring.nodeCounts = {8, 10, 12};
    cfg.coloring.radii = {2000.0, 3500.0};
    cfg.coloring.runBruteForce = true;

    Benchmark benchmark(cfg);
    benchmark.run();

    return 0;
}