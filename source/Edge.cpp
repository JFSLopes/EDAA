#include <utility>

#include "../header/Edge.h"

Edge::Edge(std::shared_ptr<Vertex> orig, std::shared_ptr<Vertex> dest, double weight, Mode mode) :
        origin(std::move(orig)), dest(std::move(dest)), weight(weight), mode(mode) { }

double Edge::getWeight() const {
    return weight;
}

std::shared_ptr<Vertex> Edge::getDest() const {
    return dest;
}

Mode Edge::getMode() const {
    return mode;
}

std::shared_ptr<Vertex> Edge::getOrigin() const {
    return origin;
}

bool Edge::isSelected() const {
    return selected;
}

void Edge::setSelected(bool cond) {
    selected = cond;
}