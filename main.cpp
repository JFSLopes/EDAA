#include "header/Benchmark.h"
#include "header/BenchmarkHardware.h"

int main() {
    //App app;
    //app.run();
    BenchmarkConfig cfg;

    Benchmark bench(cfg);
    //bench.runDijkstraVsAstar();
    //bench.runAll();

    BenchmarkHardware benchmarkHardware(bench);
    benchmarkHardware.runAll();
    return 0;
}