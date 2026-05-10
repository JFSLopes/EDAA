#ifndef EDAA_VRP_H
#define EDAA_VRP_H

#include "Multigraph.h"
#include "Quadtree.h"
#include <string>
#include <vector>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Input structs (parsed directly from JSON)
// ─────────────────────────────────────────────────────────────────────────────

struct Coord {
    double x, y;
};

struct BusInput {
    std::string id;
    int         capacity;
    Coord       depot;
    std::string depotName;
};

struct RequestInput {
    std::string id;
    Coord       origin;
    Coord       destination;
    int         people;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resolved structs (after Quadtree nearest-stop lookup)
// ─────────────────────────────────────────────────────────────────────────────

struct Stop {
    std::string  requestId;  ///< Which request this stop belongs to
    Vertex*      vertex;     ///< Nearest real graph stop
    bool         isPickup;   ///< true = pickup, false = dropoff
    int          people;     ///< People boarding (pickup) or alighting (dropoff)
};

struct BusRoute {
    BusInput            bus;
    Vertex*             depotVertex = nullptr;
    std::vector<Stop>   stops;       ///< Ordered visit sequence
    int                 load  = 0;   ///< Current passengers assigned
    double              time  = 0.0; ///< Total route travel time (seconds)
};

// ─────────────────────────────────────────────────────────────────────────────
// VRP solver
// ─────────────────────────────────────────────────────────────────────────────

class VRP {
public:
    VRP(const Multigraph& graph, const Quadtree& quadtree);

    /**
     * Load buses and requests from a JSON file.
     * Resolves every coordinate to the nearest graph vertex via the Quadtree.
     */
    void loadFromJSON(const std::string& filepath);

    /**
     * Run the two-phase VRP solver:
     *   Phase 1 — greedy assignment (largest requests first)
     *   Phase 2 — 2-opt local search per bus to minimise makespan
     * Returns false if total demand exceeds total capacity (infeasible).
     */
    bool solve(PriorityQueueSelected pqs = FIBONACCI_HEAP);

    /**
     * Print the solution: per-bus routes and makespan.
     */
    void printSolution() const;

    /**
     * Export two CSVs to dirpath/:
     *   vrp_routes.csv — one row per route segment (bus_id, leg, x1,y1,x2,y2)
     *   vrp_stops.csv  — annotated stops (bus_id, type, people_delta, load, x, y)
     * Pass the graph/ directory path, e.g. "../graph"
     */
    void exportCSV(const std::string& dirpath) const;

    [[nodiscard]] double getMakespan() const { return makespan; }
    [[nodiscard]] bool   isFeasible()  const { return feasible; }

    bool solveAndBenchmark(PriorityQueueSelected pqs = FIBONACCI_HEAP);

private:
    const Multigraph& graph;
    const Quadtree&   quadtree;

    std::vector<BusInput>     buses;
    std::vector<RequestInput> requests;
    std::vector<BusRoute>     routes;   ///< Solution — one per bus

    /// Pairwise travel-time matrix between all unique stops
    /// (depots + request origins + request destinations)
    std::vector<Vertex*>              stopIndex;    ///< Ordered unique stops
    std::unordered_map<u_int, int>    vertexToIdx;  ///< vertex id → matrix index
    std::vector<std::vector<double>>  distMatrix;   ///< distMatrix[i][j] = seconds

    double makespan = 0.0;
    bool   feasible = false;

    void buildDistanceMatrix(PriorityQueueSelected pqs);
    void greedyAssign();
    void twoOpt(BusRoute& route);
    double routeTime(const BusRoute& route) const;
    double edgeTime(Vertex* a, Vertex* b) const;
    bool solveBruteForce();
    double evaluateAssignment(const std::vector<int>& assignment, const std::vector<std::vector<int>>& orderings) const;
};

#endif //EDAA_VRP_H