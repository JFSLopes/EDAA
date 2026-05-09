#ifndef EDAA_APP_H
#define EDAA_APP_H

#include "Multigraph.h"
#include "FileParser.h"
#include <string>
#include <optional>

const std::string PYTHON_INTERPRETER = "python3.11";

class App {
public:
    App();
    void run();

private:
    Multigraph multigraph;
    std::string defaultNodesPath = "../graph/nodes_clipped.csv";
    std::string defaultEdgesPath = "../graph/edges_clipped.csv";
    std::string exportPath       = "../graph/algorithm_edges.csv";
    std::string visualizerScript = "../graph/primVisualizer.py";
    bool graphLoaded = false;

    // ── Menus ────────────────────────────────────────────────────────────────
    void menuMain();
    void menuLoadGraph();
    void menuAlgorithms();
    void menuBenchmark();

    // ── Graph loading ────────────────────────────────────────────────────────
    void loadFromFiles(const std::string& nodesPath, const std::string& edgesPath);
    void loadRandom();

    // ── Algorithms ───────────────────────────────────────────────────────────
    void runDijkstra();
    void runDijkstraFilter();
    void runPrim();

    // ── Benchmark ────────────────────────────────────────────────────────────
    void benchmarkDijkstra();
    void benchmarkPrim();

    // ── Helpers ──────────────────────────────────────────────────────────────
    std::shared_ptr<Vertex> pickVertex(const std::string& prompt);
    PriorityQueueSelected   pickPQ(const std::string& prompt);
    std::set<Mode>          pickModes();

    void exportAndVisualize(const std::vector<std::shared_ptr<Vertex>>& path,
                            const std::string& label);
    void printPath(const std::vector<std::shared_ptr<Vertex>>& path,
                   const std::string& label, double elapsed);

    void runVisualizer() const;
    void clearScreen() const;
    void printHeader() const;
    void waitEnter() const;
    bool requireGraph();

    int  readInt(const std::string& prompt, int lo, int hi);
    bool readYesNo(const std::string& prompt);
};

#endif //EDAA_APP_H