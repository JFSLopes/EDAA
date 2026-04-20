#include "../header/Multigraph.h"

Multigraph::Multigraph() : vertexSet(std::vector<std::shared_ptr<Vertex>>()) {}

std::vector<std::shared_ptr<Vertex>>& Multigraph::getVertexSet() {
    return vertexSet;
}

std::shared_ptr<Vertex> Multigraph::getVertex(u_int id) const {
    return id < vertexSet.size() ? vertexSet[id] : nullptr;
}

u_int Multigraph::addVertex(double x, double y, const std::string &name) {
    vertexSet.emplace_back(std::make_shared<Vertex>(x, y, name));
    return vertexSet.size() - 1;    // index
}

void Multigraph::addEdge(u_int source_id, u_int target_id, double weight, Mode mode) const {
    std::shared_ptr<Vertex> source = getVertex(source_id);
    std::shared_ptr<Vertex> dest = getVertex(target_id);

    source->addEdge(source, dest, weight, mode);
    dest->addEdge(dest, source, weight, mode);
}

