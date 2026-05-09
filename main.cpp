#include <iostream>
#include <chrono>
#include "header/Multigraph.h"
#include "header/FileParser.h"
#include <unordered_set>
#include <fstream>

int main() {
    /*
    auto start = std::chrono::high_resolution_clock::now();

    FileParser fileParser("../graph/nodes_clipped.csv", "../graph/edges_clipped.csv");
    Multigraph multigraph;

    std::cout << "Running\n";
    fileParser.parse(multigraph);
    size_t sum = 0;
    for (const std::shared_ptr<Vertex>& v : multigraph.getVertexSet()) {
        if (v->getAdj().empty()) std::cout << v->getCoordinates().getX() << "\n";
        sum += v->getAdj().size();
    }
    std::cout << "#Nodes: " << multigraph.getVertexSet().size() << "   #Edges: " << sum << "\n";
    std::cout << "loc_530445_4557083 has " << multigraph.getVertex(2)->getAdj().size() << " adj\n";

    auto vec = multigraph.getVertex(0);
    auto dest = multigraph.getVertex(8000);
    std::cout << "HELLO\n";
    std::cout << vec->getId() << "\n";
    //std::set<Mode> modes = {METRO, WALK};
    //std::vector<std::shared_ptr<Vertex>> ans = multigraph.dijkstra_filter(vec, dest, modes);
    std::vector<std::shared_ptr<Vertex>> ans = multigraph.dijkstra(vec, dest);
    multigraph.exportPathCSV(ans, "../graph/prim_edges.csv");

    std::unordered_set<u_int> s;

    for (auto z : ans) {
        s.insert(z->getId());
    }

    std::cout << "S: " << s.size() << "\n";

    std::cout << "#ANS: " << ans.size() << "\n";

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed: " << elapsed.count() << "s\n";

    std::cout << "End\n";
     */

    std::mt19937 rng(42);
    std::cout << "Building graph...\n";
    auto buildStart = std::chrono::high_resolution_clock::now();
    Multigraph multigraph = buildRandom(1000000, 20, 1000.0, rng);
    auto buildEnd = std::chrono::high_resolution_clock::now();
    std::cout << "Built in " << std::chrono::duration<double>(buildEnd - buildStart).count() << "s\n\n";

    auto src  = multigraph.getVertex(0);
    auto dest = multigraph.getVertex(999999);

    auto bench = [&](const std::string& name, PriorityQueueSelected pqs) {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<std::shared_ptr<Vertex>> ans = multigraph.dijkstra(src, dest, pqs);
        auto t1 = std::chrono::high_resolution_clock::now();

        std::unordered_set<u_int> s;
        for (auto& v : ans) s.insert(v->getId());

        std::cout << "=== " << name << " ===\n";
        std::cout << "  Elapsed : " << std::chrono::duration<double>(t1 - t0).count() << "s\n";
        std::cout << "  Path len: " << ans.size()      << " vertices\n";
        std::cout << "  Unique  : " << s.size()        << " vertices\n";
        std::cout << "  Distance: " << dest->getDist() << "\n";


        // Print first 10 and last 10 nodes of the path
        std::cout << "  Path    : [";
        for (size_t i = 0; i < std::min(ans.size(), size_t(10)); i++)
            std::cout << ans[i]->getId() << (i + 1 < std::min(ans.size(), size_t(10)) ? " -> " : "");
        if (ans.size() > 10) {
            std::cout << " ... ";
            for (size_t i = ans.size() - 10; i < ans.size(); i++)
                std::cout << ans[i]->getId() << (i + 1 < ans.size() ? " -> " : "");
        }
        std::cout << "]\n\n";

        multigraph.exportPathCSV(ans, "../graph/path_" + name + ".csv");
    };

    //bench("BruteForce",           BRUTE_FORCE);
    bench("MutablePriorityQueue", MUTABLE_PRIORITY_QUEUE);
    bench("FibonacciHeap",        FIBONACCI_HEAP);
    return 0;
}