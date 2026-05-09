#ifndef EDAA_VERTEX_H
#define EDAA_VERTEX_H

#include "Edge.h"
#include "Coordinate.h"
#include "Utils.h"
#include "MutablePriorityQueue.h"
#include <string>
#include <vector>
#include <memory>

class Edge;

class Vertex {
private:
    u_int id;
    Coordinate coordinate;
    std::vector<std::shared_ptr<Edge>> adjacent;
    std::string name;
    bool visited;
    double dist; ///< Used to store the distance
    std::shared_ptr<Edge> path;

public:
    Vertex(u_int id, double lat, double lon, std::string name);

    [[nodiscard]] u_int getId() const;
    [[nodiscard]] const std::vector<std::shared_ptr<Edge>>& getAdj() const;
    [[nodiscard]] const Coordinate& getCoordinates() const;
    [[nodiscard]] bool isVisited() const;
    [[nodiscard]] double getDist() const;
    [[nodiscard]] const std::shared_ptr<Edge>& getPath() const;

    void setVisited(bool cond);
    void setDist(double distance);
    void setPath(std::shared_ptr<Edge> e);

    void addEdge(const std::shared_ptr<Vertex> source, const std::shared_ptr<Vertex> dest, double weight, Mode mode);

    friend class MutablePriorityQueue;

protected:
    int queueIndex = 0; ///< Used in the mutable priority queue
};

bool operator<(const Vertex& a, const Vertex& b);

#endif //EDAA_VERTEX_H
