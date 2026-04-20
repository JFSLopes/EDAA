#include "../header/Vertex.h"

Vertex::Vertex(double lat, double lon, std::string name) :
    coordinate(Coordinate(lat, lon)), adjacent(std::vector<std::shared_ptr<Edge>>()), name(std::move(name)), visited(false) {}

const std::vector<std::shared_ptr<Edge>>& Vertex::getAdj() const {
    return adjacent;
}

const Coordinate& Vertex::getCoordinates() const {
    return coordinate;
}

bool Vertex::isVisited() const {
    return visited;
}

void Vertex::setVisited(bool cond) {
    visited = cond;
}

void Vertex::addEdge(const std::shared_ptr<Vertex> source, const std::shared_ptr<Vertex> dest, double weight, Mode mode) {
    std::shared_ptr<Edge> edge = std::make_shared<Edge>(source, dest, weight, mode);
    adjacent.emplace_back(edge);
}
