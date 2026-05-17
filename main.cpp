#include "header/Benchmark.h"

int main() {
    //App app;
    //app.run();
    BenchmarkConfig cfg;

    Benchmark bench(cfg);
    //bench.runDijkstraVsAstar();
    bench.runAll();
    return 0;
}