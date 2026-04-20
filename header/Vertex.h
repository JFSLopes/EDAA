#ifndef EDAA_VERTEX_H
#define EDAA_VERTEX_H

#include "Edge.h"
#include "Coordinate.h"
#include "Utils.h"
#include <string>
#include <vector>
#include <memory>

class Edge;

class Vertex {
private:
    Coordinate coordinate;
    std::vector<std::shared_ptr<Edge>> adjacent;
    std::string name;
    bool visited;

public:
    Vertex(double lat, double lon, std::string name);

    [[nodiscard]] const std::vector<std::shared_ptr<Edge>>& getAdj() const;
    [[nodiscard]] const Coordinate& getCoordinates() const;
    [[nodiscard]] bool isVisited() const;
    void setVisited(bool cond);

    void addEdge(const std::shared_ptr<Vertex> source, const std::shared_ptr<Vertex> dest, double weight, Mode mode);
};


#endif //EDAA_VERTEX_H
