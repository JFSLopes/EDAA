#include "../header/Coordinate.h"
#include <cmath>

Coordinate::Coordinate(double x, double y) : x(x), y(y) {}

double Coordinate::getX() const {
    return x;
}

double Coordinate::getY() const {
    return y;
}

double Coordinate::distanceTo(const Coordinate& other) const {
    double dx = x - other.getX();
    double dy = y - other.getY();
    return std::sqrt(dx * dx + dy * dy);
}
