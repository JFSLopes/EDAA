#ifndef EDAA_QUADTREE_H
#define EDAA_QUADTREE_H

#include "Vertex.h"
#include <vector>
#include <memory>
#include <limits>

struct QuadtreeStats {
    long long boxChecks = 0;        // How many node bounding boxes were tested
    long long nodesVisited = 0;     // How many quadtree nodes were visited
    long long nodesPruned = 0;      // How many nodes were skipped
    long long distanceChecks = 0;   // How many point distance checks were done
};

/**
 * Axis-aligned bounding box used by the Quadtree.
 */
struct AABB {
    double cx, cy;   ///< Centre
    double hw, hh;   ///< Half-width, half-height

    AABB(double cx, double cy, double hw, double hh)
            : cx(cx), cy(cy), hw(hw), hh(hh) {}

    [[nodiscard]] bool contains(double x, double y) const;
    [[nodiscard]] bool intersectsCircle(double x, double y, double r) const;
};

/**
 * Point-region Quadtree over the Multigraph vertex set.
 *
 * Provides O(log n) nearest-neighbour lookup — used by the COLORING solver
 * to map arbitrary UTM coordinates to real graph stops.
 */
class Quadtree {
    static constexpr int CAPACITY = 8; ///< Max vertices per leaf before splitting

    struct Node {
        AABB boundary;
        std::vector<Vertex*> points;
        std::unique_ptr<Node> nw, ne, sw, se;
        bool divided = false;

        explicit Node(AABB b) : boundary(b) {}

        void subdivide();
        bool insert(Vertex* v);
        void rangeSearch(double x, double y, double r, std::vector<Vertex*>& out, QuadtreeStats* stats = nullptr) const;
    };

    std::unique_ptr<Node> root;

public:
    /**
     * Build the Quadtree from a vertex set.
     * Computes the bounding box automatically.
     */
    explicit Quadtree(const std::vector<std::shared_ptr<Vertex>>& vertices);

    /**
     * Find all vertices within radius r of (x, y).
     */
    [[nodiscard]] std::vector<Vertex*> rangeSearch(double x, double y, double r) const;
    [[nodiscard]] std::vector<Vertex*> rangeSearch(double x, double y, double r, QuadtreeStats& stats) const;
};

#endif //EDAA_QUADTREE_H