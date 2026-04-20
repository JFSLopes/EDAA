#ifndef EDAA_EDGE_H
#define EDAA_EDGE_H

#include "Vertex.h"
#include "Utils.h"
#include <memory>

class Vertex;

class Edge {
private:
    std::shared_ptr<Vertex> origin;
    std::shared_ptr<Vertex> dest;
    double weight;  ///< Time in seconds
    Mode mode;

public:
    Edge(std::shared_ptr<Vertex> origin, std::shared_ptr<Vertex> dest, double weight, Mode mode);

    [[nodiscard]] double getWeight() const;
    [[nodiscard]] std::shared_ptr<Vertex> getDest() const;
    [[nodiscard]] std::shared_ptr<Vertex> getOrigin() const;
};


#endif //EDAA_EDGE_H
