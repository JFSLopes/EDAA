#ifndef EDAA_MULTIGRAPH_H
#define EDAA_MULTIGRAPH_H

#include "Vertex.h"
#include "FibonacciHeap.h"
#include "MutablePriorityQueue.h"
#include "BruteForceQueue.h"
#include <unordered_map>
#include <set>
#include <random>

enum PriorityQueueSelected {
    BRUTE_FORCE = 0,
    MUTABLE_PRIORITY_QUEUE = 1,
    FIBONACCI_HEAP = 2
};

class Multigraph {
private:
    std::vector<std::shared_ptr<Vertex>> vertexSet; ///< Vector tha holds all vertexes that belong to the graph

public:
    Multigraph();

    [[nodiscard]] std::vector<std::shared_ptr<Vertex>>& getVertexSet();
    [[nodiscard]] std::shared_ptr<Vertex> getVertex(u_int id) const;

    u_int addVertex(double x, double y, const std::string& name);
    void addEdge(u_int source_id, u_int target_id, double weight, Mode mode) const;

    /// Algorithms
    [[nodiscard]] std::vector<std::shared_ptr<Vertex>> prim(const std::shared_ptr<Vertex>& s, const PriorityQueueSelected pqs) const;

    [[nodiscard]] std::vector<std::shared_ptr<Vertex>> dijkstra(const std::shared_ptr<Vertex>& src, const std::shared_ptr<Vertex>& dest, const PriorityQueueSelected pqs) const;
    void dijkstra_aux(const std::shared_ptr<Vertex>& src, const PriorityQueueSelected pqs) const;

    [[nodiscard]] std::vector<std::shared_ptr<Vertex>> dijkstra_filter(const std::shared_ptr<Vertex>& src, const std::shared_ptr<Vertex>& dest, const std::set<Mode>& modes, const PriorityQueueSelected pqs) const;
    void dijkstra_filter_aux(const std::shared_ptr<Vertex>& src, const std::set<Mode>& modes, const PriorityQueueSelected pqs) const;

    /** Creates a CSV with the points belonging to the path */
    void exportPathCSV(const std::vector<std::shared_ptr<Vertex>>& path, const std::string& filepath) const;
};

static std::unique_ptr<PriorityQueue> makePQ(PriorityQueueSelected pqs) {
    switch (pqs) {
        case FIBONACCI_HEAP:        return std::make_unique<FibonacciHeap>();
        case MUTABLE_PRIORITY_QUEUE: return std::make_unique<MutablePriorityQueue>();
        case BRUTE_FORCE:           return std::make_unique<BruteForceQueue>();
    }
}

static Multigraph buildRandom(int V, int avgDeg, double wMax, std::mt19937& rng) {
    Multigraph g;
    std::uniform_real_distribution<double> coord(0.0, 1000.0);
    std::uniform_real_distribution<double> weight(1.0, wMax);
    std::uniform_int_distribution<int>     modeD(0, 2);
    std::uniform_int_distribution<int>     vtxD(0, V - 1);

    for (int i = 0; i < V; i++)
        g.addVertex(coord(rng), coord(rng), "v" + std::to_string(i));

    // Spanning tree first (guarantees connectivity)
    for (int i = 1; i < V; i++) {
        int j = std::uniform_int_distribution<int>(0, i - 1)(rng);
        Mode m = static_cast<Mode>(modeD(rng));
        g.addEdge(i, j, weight(rng), m);
    }

    // Extra random edges
    int extra = V * avgDeg / 2 - (V - 1);
    std::set<std::pair<int,int>> seen;
    for (int k = 0; k < extra * 3 && (int)seen.size() < extra; k++) {
        int a = vtxD(rng), b = vtxD(rng);
        if (a == b) continue;
        if (a > b) std::swap(a, b);
        if (seen.count({a, b})) continue;
        seen.insert({a, b});
        Mode m = static_cast<Mode>(modeD(rng));
        g.addEdge(a, b, weight(rng), m);
    }
    return g;
}

#endif //EDAA_MULTIGRAPH_H
