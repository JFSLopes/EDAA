#include "../header/Vertex.h"

Vertex::Vertex(u_int id, double lat, double lon, std::string name) :
    id(id), coordinate(Coordinate(lat, lon)), adjacent(std::vector<std::shared_ptr<Edge>>()), name(std::move(name)), visited(false) {}

u_int Vertex::getId() const{
    return id;
}

const std::vector<std::shared_ptr<Edge>>& Vertex::getAdj() const {
    return adjacent;
}

const Coordinate& Vertex::getCoordinates() const {
    return coordinate;
}

bool Vertex::isVisited() const {
    return visited;
}

double Vertex::getDist() const {
    return dist;
}

const std::shared_ptr<Edge> &Vertex::getPath() const {
    return path;
}

void Vertex::setVisited(bool cond) {
    visited = cond;
}

void Vertex::setDist(double distance) {
    dist = distance;
}

void Vertex::setPath(std::shared_ptr<Edge> e) {
    path = e;
}

void Vertex::addEdge(const std::shared_ptr<Vertex> source, const std::shared_ptr<Vertex> dest, double weight, Mode mode) {
    std::shared_ptr<Edge> edge = std::make_shared<Edge>(source, dest, weight, mode);
    adjacent.emplace_back(edge);
}

bool operator<(const Vertex& a, const Vertex& b){
    return a.getDist() < b.getDist();
}