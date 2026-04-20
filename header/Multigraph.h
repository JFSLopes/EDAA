#ifndef EDAA_MULTIGRAPH_H
#define EDAA_MULTIGRAPH_H

#include "Vertex.h"
#include <unordered_map>

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
    [[nodiscard]] std::vector<std::shared_ptr<Vertex>> prim(const std::shared_ptr<Vertex>& s) const;
    void exportPrimCSV(const std::vector<std::shared_ptr<Vertex>>& ans, const std::string& filepath) const;

};


#endif //EDAA_MULTIGRAPH_H
