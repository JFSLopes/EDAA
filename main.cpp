#include <iostream>
#include <chrono>
#include "header/Multigraph.h"
#include "header/FileParser.h"
#include <unordered_set>
#include <fstream>

int main() {
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

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed: " << elapsed.count() << "s\n";

    auto vec = multigraph.getVertex(0);
    std::cout << "HELLO\n";
    std::cout << vec->getId() << "\n";
    std::vector<std::shared_ptr<Vertex>> ans = multigraph.prim(vec);

    multigraph.exportPrimCSV(ans, "../graph/prim_edges.csv");

    std::unordered_set<u_int> s;

    for (auto z : ans) {
        s.insert(z->getId());
    }

    std::cout << "S: " << s.size() << "\n";

    std::cout << "#ANS: " << ans.size() << "\n";

    std::cout << "End\n";
    return 0;
}