#include "header/Benchmark.h"

int main() {
    Benchmark::Config cfg;

    cfg.outputDirectory = "../benchmark";

    Benchmark benchmark(cfg);
    benchmark.run();

    return 0;
}