#include "../header/Coordinate.h"
#include <cmath>

Coordinate::Coordinate(double lat, double lon) : lat(lat), lon(lon) {}

double Coordinate::getLat() const {
    return lat;
}

double haversine(const Coordinate& c1, const Coordinate& c2){
    double lon1_rad = c1.lon * M_PI / 180.0;
    double lat1_rad = c1.lat * M_PI / 180.0;
    double lon2_rad = c2.lon * M_PI / 180.0;
    double lat2_rad = c2.lat * M_PI / 180.0;

    double dlon = lon2_rad - lon1_rad;
    double dlat = lat2_rad - lat1_rad;
    double a = pow(sin(dlat / 2), 2) + cos(lat1_rad) * cos(lat2_rad) * pow(sin(dlon / 2), 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));

    const double earth_radius_m = 6371000.0;

    double distance = earth_radius_m * c;
    return distance;
}
