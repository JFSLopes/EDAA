#include <iostream>
#include <chrono>
#include "header/Multigraph.h"
#include "header/FileParser.h"

int main() {
    auto start = std::chrono::high_resolution_clock::now();

    FileParser fileParser("../graph/nodes_clipped.csv", "../graph/edges_clipped.csv");
    Multigraph multigraph;

    std::cout << "Running\n";
    fileParser.parse(multigraph);
    size_t sum = 0;
    for (const std::shared_ptr<Vertex>& v : multigraph.getVertexSet()) {
        if (v->getAdj().empty()) std::cout << v->getCoordinates().getLat() << "\n";
        sum += v->getAdj().size();
    }
    std::cout << "#Nodes: " << multigraph.getVertexSet().size() << "   #Edges: " << sum << "\n";
    std::cout << "loc_530445_4557083 has " << multigraph.getVertex(2)->getAdj().size() << " adj\n";

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed: " << elapsed.count() << "s\n";

    std::cout << "End\n";
    return 0;
}