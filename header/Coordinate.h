#ifndef EDAA_COORDINATE_H
#define EDAA_COORDINATE_H


class Coordinate {
private:
    const double x;
    const double y;
public:
    Coordinate (double x, double y);

    [[nodiscard]] double getX() const;
    [[nodiscard]] double getY() const;
};


#endif //EDAA_COORDINATE_H
