#include "../header/Coloring.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

double Coloring::euclidean(double x1, double y1, double x2, double y2) {
    double dx = x1 - x2, dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

// ─────────────────────────────────────────────────────────────────────────────
// Getters
// ─────────────────────────────────────────────────────────────────────────────

const std::vector<Coloring::Antenna>& Coloring::getAntennas() const {
    return antennas;
}

const std::vector<std::vector<int>>& Coloring::getAdjacency() const {
    return adj;
}

double Coloring::getInterferenceRadius() const {
    return interferenceRadius;
}

size_t Coloring::getConflictEdgeCount() const {
    size_t total = 0;
    for (auto& row : adj) total += row.size();
    return total / 2; // undirected
}

size_t Coloring::getAntennaCount() const {
    return antennas.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON loading
// Hand-rolled parser matching:
// {
//   "interference_radius": 700.0,
//   "antennas": [{"id":"ap_1","x":...,"y":...}, ...]
// }
// ─────────────────────────────────────────────────────────────────────────────

size_t Coloring::loadFromJson(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open())
        throw std::runtime_error("Coloring: cannot open " + filepath);

    std::string json((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

    auto strVal = [&](const std::string& key, size_t from) -> std::pair<std::string, size_t> {
        std::string token = "\"" + key + "\"";
        size_t pos = json.find(token, from);
        if (pos == std::string::npos) return {"", std::string::npos};
        pos = json.find(':', pos) + 1;
        size_t q1 = json.find('"', pos) + 1;
        size_t q2 = json.find('"', q1);
        return {json.substr(q1, q2 - q1), q2};
    };

    auto numVal = [&](const std::string& key, size_t from) -> std::pair<double, size_t> {
        std::string token = "\"" + key + "\"";
        size_t pos = json.find(token, from);
        if (pos == std::string::npos) return {0.0, std::string::npos};
        pos = json.find(':', pos) + 1;
        while (pos < json.size() &&
               (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r')) pos++;
        size_t end = pos;
        while (end < json.size() &&
               (std::isdigit(json[end]) || json[end] == '.' || json[end] == '-')) end++;
        return {std::stod(json.substr(pos, end - pos)), end};
    };

    antennas.clear();
    adj.clear();

    // Parse interference_radius
    auto [radius, rp] = numVal("interference_radius", 0);
    interferenceRadius = radius;

    // Parse antennas array
    size_t arrStart = json.find('[');
    if (arrStart == std::string::npos)
        throw std::runtime_error("Coloring JSON: missing antennas array");

    size_t pos = arrStart;
    while (true) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) break;

        Antenna a;
        auto [id, ip] = strVal("id", objStart); a.id = id;
        if (a.id.empty()) break;
        auto [x, xp]  = numVal("x", objStart); a.x = x;
        auto [y, yp]  = numVal("y", objStart); a.y = y;
        antennas.push_back(a);

        pos = json.find('}', objStart) + 1;
    }

    std::cout << "  Loaded " << antennas.size() << " antennas"
              << "  interference_radius=" << interferenceRadius << "m\n";

    size_t edges = buildConflictGraph();
    return edges;
}

// ─────────────────────────────────────────────────────────────────────────────
// Build conflict graph
// Two antennas conflict if their Euclidean distance <= interferenceRadius.
// Uses a Quadtree for O(n log n) construction instead of O(n²).
// ─────────────────────────────────────────────────────────────────────────────

size_t Coloring::buildConflictGraph() {
    int N = (int)antennas.size();
    adj.assign(N, {});

    // Build a temporary vertex set for the Quadtree
    // Each antenna becomes a Vertex with matching coordinates
    std::vector<std::shared_ptr<Vertex>> tempVerts;
    tempVerts.reserve(N);
    for (int i = 0; i < N; i++)
        tempVerts.push_back(std::make_shared<Vertex>(i, antennas[i].x, antennas[i].y, antennas[i].id));

    Quadtree qt(tempVerts);

    size_t edges = 0;
    for (int i = 0; i < N; i++) {
        std::vector<Vertex*> nearby = qt.rangeSearch(antennas[i].x, antennas[i].y,
                                                     interferenceRadius);
        for (Vertex* v : nearby) {
            int j = (int)v->getId();
            if (j <= i) continue; // avoid duplicates and self-loops
            adj[i].push_back(j);
            adj[j].push_back(i);
            edges++;
        }
    }

    std::cout << "  Conflict graph: " << N << " antennas, "
              << edges << " interference edges\n";

    return edges;
}

// ─────────────────────────────────────────────────────────────────────────────
// Coloring helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Coloring::canUseColor(int antennaIdx, int color,
                           const std::vector<int>& colors) const {
    for (int nb : adj[antennaIdx])
        if (colors[nb] == color) return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Brute force — exact chromatic number
// Tries k=1,2,... until a valid k-coloring exists.
// Uses backtracking with pruning.
// ─────────────────────────────────────────────────────────────────────────────

bool Coloring::bruteForceRec(int idx,
                             int maxColors,
                             const std::vector<int>& order,
                             std::vector<int>& colors) const {
    if (idx == (int)order.size()) return true;
    int i = order[idx];
    for (int c = 0; c < maxColors; c++) {
        if (canUseColor(i, c, colors)) {
            colors[i] = c;
            if (bruteForceRec(idx + 1, maxColors, order, colors)) return true;
            colors[i] = -1;
        }
    }
    return false;
}

Coloring::Solution Coloring::solveBruteForce() const {
    int N = (int)antennas.size();

    // Clique bound: chromatic number >= max clique size
    // Simple lower bound: max degree gives upper bound of degree+1
    int maxDeg = 0;
    for (auto& row : adj) maxDeg = std::max(maxDeg, (int)row.size());

    // Order vertices by degree descending for better pruning
    std::vector<int> order(N);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return adj[a].size() > adj[b].size();
    });

    Solution sol;
    sol.colorOf.assign(N, -1);

    for (int k = 1; k <= N; k++) {
        std::fill(sol.colorOf.begin(), sol.colorOf.end(), -1);
        if (bruteForceRec(0, k, order, sol.colorOf)) {
            sol.feasible   = true;
            sol.colorsUsed = k;
            std::cout << "  Found valid " << k << "-coloring\n";
            return sol;
        }
    }

    // Should never reach here (N-coloring always exists)
    sol.feasible   = true;
    sol.colorsUsed = N;
    return sol;
}

// ─────────────────────────────────────────────────────────────────────────────
// Welsh-Powell greedy coloring
// 1. Sort antennas by degree descending
// 2. Assign lowest available color not used by any neighbour
// Fast but not guaranteed optimal — typically very close
// ─────────────────────────────────────────────────────────────────────────────

Coloring::Solution Coloring::solveWelshPowell() const {
    int N = (int)antennas.size();

    // Step 1 & 2: sort vertices by degree descending
    std::vector<int> order(N);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return adj[a].size() > adj[b].size();
    });

    Solution sol;
    sol.colorOf.assign(N, -1);
    int colorsUsed = 0;

    // Steps 3-5: one pass per color
    int currentColor = 0;
    while (true) {
        // Check if all vertices are colored
        bool anyUncolored = false;
        for (int i = 0; i < N; i++)
            if (sol.colorOf[i] == -1) { anyUncolored = true; break; }
        if (!anyUncolored) break;

        // Walk the ordered list and assign currentColor to every uncolored
        // vertex that has no neighbour already holding currentColor
        for (int i : order) {
            if (sol.colorOf[i] != -1) continue; // already colored

            bool conflict = false;
            for (int nb : adj[i])
                if (sol.colorOf[nb] == currentColor) { conflict = true; break; }

            if (!conflict)
                sol.colorOf[i] = currentColor;
        }

        colorsUsed = ++currentColor;
    }

    sol.feasible   = true;
    sol.colorsUsed = colorsUsed;
    return sol;
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

bool Coloring::isValid(const Solution& solution) const {
    int N = (int)antennas.size();
    if ((int)solution.colorOf.size() != N) return false;
    for (int i = 0; i < N; i++) {
        if (solution.colorOf[i] < 0) return false;
        for (int nb : adj[i])
            if (solution.colorOf[nb] == solution.colorOf[i]) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Print solution
// ─────────────────────────────────────────────────────────────────────────────

void Coloring::printSolution(const Solution& solution) const {
    const std::string SEP  = "  " + std::string(62, '-') + "\n";
    const std::string SEP2 = "  " + std::string(62, '=') + "\n";

    std::cout << "\n" << SEP2;
    std::cout << "  GRAPH COLORING SOLUTION\n";
    std::cout << SEP2;
    std::cout << "  Antennas        : " << antennas.size()       << "\n";
    std::cout << "  Conflict edges  : " << getConflictEdgeCount() << "\n";
    std::cout << "  Interference r  : " << interferenceRadius     << " m\n";
    std::cout << "  Colors (channels): " << solution.colorsUsed   << "\n";
    std::cout << "  Valid           : " << (isValid(solution) ? "YES ✓" : "NO ✗") << "\n";
    std::cout << SEP;

    // Group antennas by color
    std::vector<std::vector<int>> groups(solution.colorsUsed);
    for (int i = 0; i < (int)antennas.size(); i++)
        if (solution.colorOf[i] >= 0)
            groups[solution.colorOf[i]].push_back(i);

    for (int c = 0; c < solution.colorsUsed; c++) {
        std::cout << "  Channel " << std::setw(2) << c << " ("
                  << std::setw(3) << groups[c].size() << " antennas):  ";
        for (int i : groups[c])
            std::cout << antennas[i].id << "  ";
        std::cout << "\n";
    }
    std::cout << SEP2;
}

// ─────────────────────────────────────────────────────────────────────────────
// Export CSV for visualizer
// vertices: id,x,y,color
// edges   : x1,y1,x2,y2   (interference edges)
// ─────────────────────────────────────────────────────────────────────────────

void Coloring::exportCSV(const Solution& solution,
                         const std::string& verticesPath,
                         const std::string& edgesPath) const {
    // Vertices
    {
        std::ofstream out(verticesPath);
        if (!out.is_open())
            throw std::runtime_error("Coloring: cannot write " + verticesPath);
        out << "id,x,y,color\n";
        out << std::fixed << std::setprecision(3);
        for (int i = 0; i < (int)antennas.size(); i++) {
            out << antennas[i].id << ","
                << antennas[i].x  << ","
                << antennas[i].y  << ","
                << solution.colorOf[i] << "\n";
        }
    }

    // Conflict edges
    {
        std::ofstream out(edgesPath);
        if (!out.is_open())
            throw std::runtime_error("Coloring: cannot write " + edgesPath);
        out << "x1,y1,x2,y2\n";
        out << std::fixed << std::setprecision(3);
        for (int i = 0; i < (int)antennas.size(); i++) {
            for (int j : adj[i]) {
                if (j <= i) continue;
                out << antennas[i].x << ","
                    << antennas[i].y << ","
                    << antennas[j].x << ","
                    << antennas[j].y << "\n";
            }
        }
    }

    std::cout << "  Exported → " << verticesPath << "\n";
    std::cout << "  Exported → " << edgesPath    << "\n";
}