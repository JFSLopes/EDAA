#include "../header/Quadtree.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// AABB
// ─────────────────────────────────────────────────────────────────────────────

bool AABB::contains(double x, double y) const {
    return x >= cx - hw && x <= cx + hw
           && y >= cy - hh && y <= cy + hh;
}

bool AABB::intersectsCircle(double x, double y, double r) const {
    /// Closest point on the box to the query point
    double closestX = std::clamp(x, cx - hw, cx + hw);
    double closestY = std::clamp(y, cy - hh, cy + hh);
    double dx = x - closestX;
    double dy = y - closestY;
    return dx * dx + dy * dy <= r * r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Quadtree::Node
// ─────────────────────────────────────────────────────────────────────────────

void Quadtree::Node::subdivide() {
    double hw = boundary.hw / 2.0;
    double hh = boundary.hh / 2.0;
    double cx = boundary.cx;
    double cy = boundary.cy;

    nw = std::make_unique<Node>(AABB(cx - hw, cy + hh, hw, hh));
    ne = std::make_unique<Node>(AABB(cx + hw, cy + hh, hw, hh));
    sw = std::make_unique<Node>(AABB(cx - hw, cy - hh, hw, hh));
    se = std::make_unique<Node>(AABB(cx + hw, cy - hh, hw, hh));
    divided = true;

    /// Re-insert existing points into children
    for (Vertex* v : points) {
        double vx = v->getCoordinates().getX();
        double vy = v->getCoordinates().getY();
        if      (nw->boundary.contains(vx, vy)) nw->points.push_back(v);
        else if (ne->boundary.contains(vx, vy)) ne->points.push_back(v);
        else if (sw->boundary.contains(vx, vy)) sw->points.push_back(v);
        else                                     se->points.push_back(v);
    }
    points.clear();
}

bool Quadtree::Node::insert(Vertex* v) {
    double vx = v->getCoordinates().getX();
    double vy = v->getCoordinates().getY();

    if (!boundary.contains(vx, vy)) return false;

    if (!divided) {
        if ((int)points.size() < CAPACITY) {
            points.push_back(v);
            return true;
        }
        subdivide();
    }

    return nw->insert(v) || ne->insert(v)
           || sw->insert(v) || se->insert(v);
}

void Quadtree::Node::nearest(double x, double y,
                             Vertex*& best, double& bestDist) const {
    /// Prune: if this box can't contain anything closer than bestDist, skip
    if (!boundary.intersectsCircle(x, y, std::sqrt(bestDist))) return;

    if (!divided) {
        for (Vertex* v : points) {
            double dx = v->getCoordinates().getX() - x;
            double dy = v->getCoordinates().getY() - y;
            double d2 = dx * dx + dy * dy;
            if (d2 < bestDist) {
                bestDist = d2;
                best     = v;
            }
        }
        return;
    }

    /// Visit children in order of proximity to query point
    /// so pruning kicks in as early as possible
    std::array<Node*, 4> children = { nw.get(), ne.get(), sw.get(), se.get() };
    std::sort(children.begin(), children.end(), [&](Node* a, Node* b) {
        auto dist2 = [&](Node* n) {
            double dx = std::clamp(x, n->boundary.cx - n->boundary.hw,
                                   n->boundary.cx + n->boundary.hw) - x;
            double dy = std::clamp(y, n->boundary.cy - n->boundary.hh,
                                   n->boundary.cy + n->boundary.hh) - y;
            return dx * dx + dy * dy;
        };
        return dist2(a) < dist2(b);
    });

    for (Node* child : children)
        child->nearest(x, y, best, bestDist);
}

void Quadtree::Node::rangeSearch(double x, double y, double r,
                                 std::vector<Vertex*>& out) const {
    if (!boundary.intersectsCircle(x, y, r)) return;

    if (!divided) {
        for (Vertex* v : points) {
            double dx = v->getCoordinates().getX() - x;
            double dy = v->getCoordinates().getY() - y;
            if (dx * dx + dy * dy <= r * r)
                out.push_back(v);
        }
        return;
    }

    nw->rangeSearch(x, y, r, out);
    ne->rangeSearch(x, y, r, out);
    sw->rangeSearch(x, y, r, out);
    se->rangeSearch(x, y, r, out);
}

// ─────────────────────────────────────────────────────────────────────────────
// Quadtree
// ─────────────────────────────────────────────────────────────────────────────
Quadtree::Quadtree(const std::vector<std::shared_ptr<Vertex>>& vertices) {
    if (vertices.empty())
        throw std::runtime_error("Quadtree: empty vertex set");

    /// Compute bounding box
    double minX =  std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double minY =  std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();

    for (const auto& v : vertices) {
        double x = v->getCoordinates().getX();
        double y = v->getCoordinates().getY();
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }

    double cx = (minX + maxX) / 2.0;
    double cy = (minY + maxY) / 2.0;
    double hw = (maxX - minX) / 2.0 * 1.01; // tiny padding so boundary.contains works on extremes
    double hh = (maxY - minY) / 2.0 * 1.01;

    root = std::make_unique<Node>(AABB(cx, cy, hw, hh));

    for (const auto& v : vertices)
        root->insert(v.get());
}

Vertex* Quadtree::nearest(double x, double y) const {
    Vertex* best    = nullptr;
    double  bestDist = std::numeric_limits<double>::max();
    root->nearest(x, y, best, bestDist);
    return best;
}

std::vector<Vertex*> Quadtree::rangeSearch(double x, double y, double r) const {
    std::vector<Vertex*> out;
    root->rangeSearch(x, y, r, out);
    return out;
}