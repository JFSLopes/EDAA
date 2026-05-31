#ifndef COLORING_H
#define COLORING_H

#include "Multigraph.h"
#include "Quadtree.h"

#include <string>
#include <vector>

class Coloring {
public:
    struct Antenna {
        std::string id;
        double x = 0.0;
        double y = 0.0;
    };

    struct Solution {
        bool feasible = false;
        int colorsUsed = 0;
        std::vector<int> colorOf; // colorOf[i] is the channel/color assigned to antenna i
    };

    Coloring() = default;

    // Reads JSON with:
    // {
    //   "interference_radius": 700.0,
    //   "antennas": [{"id":"ap_1", "x":..., "y":...}, ...]
    // }
    size_t loadFromJson(const std::string& filepath);

    // Exact solver. Tries k = 1, 2, ... until a valid coloring is found.
    // Best for small instances.
    Solution solveBruteForce() const;

    // Fast heuristic: Welsh-Powell greedy coloring.
    Solution solveWelshPowell() const;

    bool isValid(const Solution& solution) const;
    void printSolution(const Solution& solution) const;

    // Exports two files for the visualizer:
    // vertices CSV: id,x,y,color
    // edges CSV   : x1,y1,x2,y2
    void exportCSV(const Solution& solution,
                   const std::string& verticesPath,
                   const std::string& edgesPath) const;

    const std::vector<Antenna>& getAntennas() const;
    const std::vector<std::vector<int>>& getAdjacency() const;
    double getInterferenceRadius() const;
    size_t getConflictEdgeCount() const;
    size_t getAntennaCount() const;

private:
    double interferenceRadius = 0.0;
    std::vector<Antenna> antennas;
    std::vector<std::vector<int>> adj;
    Multigraph conflictGraph;

    size_t buildConflictGraph();
    bool canUseColor(int antennaIndex, int color, const std::vector<int>& colors) const;
    bool bruteForceRec(int idx,
                       int maxColors,
                       const std::vector<int>& order,
                       std::vector<int>& colors) const;

    static double euclidean(double x1, double y1, double x2, double y2);
};

#endif
