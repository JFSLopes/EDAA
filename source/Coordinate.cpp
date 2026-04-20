#include "../header/Coordinate.h"
#include <cmath>

Coordinate::Coordinate(double x, double y) : x(x), y(y) {}

double Coordinate::getX() const {
    return x;
}

double Coordinate::getY() const {
    return y;
}
