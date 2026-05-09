#ifndef EDAA_MULTIGRAPH_H
#define EDAA_MULTIGRAPH_H

#include "Vertex.h"
#include "FibonacciHeap.h"
#include "MutablePriorityQueue.h"
#include "BruteForceQueue.h"
#include <unordered_map>
#include <set>
#include <iostream>
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

    [[nodiscard]] const std::vector<std::shared_ptr<Vertex>>& getVertexSet() const;
    [[nodiscard]] std::shared_ptr<Vertex> getVertex(u_int id) const;

    u_int addVertex(double x, double y, const std::string& name);
    void addEdge(u_int source_id, u_int target_id, double weight, Mode mode) const;

    /// Algorithms
    [[nodiscard]] std::vector<std::shared_ptr<Vertex>> prim(const std::shared_ptr<Vertex>& s, PriorityQueueSelected pqs) const;

    [[nodiscard]] std::vector<std::shared_ptr<Vertex>> dijkstra(const std::shared_ptr<Vertex>& src, const std::shared_ptr<Vertex>& dest, PriorityQueueSelected pqs) const;
    void dijkstra_aux(const std::shared_ptr<Vertex>& src, PriorityQueueSelected pqs) const;

    [[nodiscard]] std::vector<std::shared_ptr<Vertex>> dijkstra_filter(const std::shared_ptr<Vertex>& src, const std::shared_ptr<Vertex>& dest, const std::set<Mode>& modes, PriorityQueueSelected pqs) const;
    void dijkstra_filter_aux(const std::shared_ptr<Vertex>& src, const std::set<Mode>& modes, PriorityQueueSelected pqs) const;

    [[nodiscard]] std::vector<std::shared_ptr<Vertex>> astar(const std::shared_ptr<Vertex>& src, const std::shared_ptr<Vertex>& dest, PriorityQueueSelected pqs) const;
    void astar_aux(const std::shared_ptr<Vertex>& src, const std::shared_ptr<Vertex>& dest, PriorityQueueSelected pqs) const;

    /** Creates a CSV with the points belonging to the path */
    void exportPathCSV(const std::vector<std::shared_ptr<Vertex>>& path, const std::string& filepath) const;
};

static std::unique_ptr<PriorityQueue> makePQ(PriorityQueueSelected pqs) {

    switch (pqs) {
        case FIBONACCI_HEAP:
            std::cout << "Running FIBONACCI HEAP\n";
            return std::make_unique<FibonacciHeap>();

        case MUTABLE_PRIORITY_QUEUE:
            std::cout << "Running MUTABLE PRIORITY QUEUE\n";
            return std::make_unique<MutablePriorityQueue>();

        case BRUTE_FORCE:
            std::cout << "Running BRUTE FORCE HEAP\n";
            return std::make_unique<BruteForceQueue>();
    }
}

#endif //EDAA_MULTIGRAPH_H
