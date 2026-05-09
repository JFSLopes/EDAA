#ifndef EDAA_UTILS_H
#define EDAA_UTILS_H

#include <string>

const std::string PYTHON_INTERPRETER = "python3.11";
const double SPEED_WALK  = 1.4;  /// m/s  ≈  5.0 km/h
const double SPEED_BUS   = 6.9;  /// m/s  ≈ 25.0 km/h  (STCP urban avg incl. stops)
const double SPEED_METRO = 11.1; /// m/s  ≈ 40.0 km/h  (Metro do Porto commercial avg)
const double ADMISSIBILITY_SAFETY = 0.99;

enum Mode {
    WALK,
    BUS,
    METRO
};

#endif //EDAA_UTILS_H
