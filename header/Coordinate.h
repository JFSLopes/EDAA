#ifndef EDAA_COORDINATE_H
#define EDAA_COORDINATE_H


class Coordinate {
private:
    const double lat; ///< stores the latitude of a coordinate
    const double lon; ///< stores the longitude of a coordinate
public:
    /**
     * @brief Coordinates constructor
     */
    Coordinate (double lat, double lon);
    /**
     * @brief Returns the latitude
     */
    double getLat() const;
    /**
     * @brief Calculates the distance between two given coordinates
     *
     * This method uses the haversine formula to calculate the geographical distance between two given coordinates on earth
     *
     * @param c1 First coordinate
     * @param c2 Second coordinate
     * @return Returns the distance between the coordinates
     */
    friend double haversine(const Coordinate& c1, const Coordinate& c2);
};
/**
 * @brief Calculates the distance between 2 geographic coordinates
 * @param c1 Geographical coordinate
 * @param c2 Geographical coordinate
 * @return Returns the distance between c1 and c2
 */
double haversine(const Coordinate& c1, const Coordinate& c2);


#endif //EDAA_COORDINATE_H
