#include "../header/VRP.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <limits>
#include <set>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string fmtTime(double seconds) {
    int h = (int)seconds / 3600;
    int m = ((int)seconds % 3600) / 60;
    int s = (int)seconds % 60;
    std::ostringstream oss;
    if (h > 0) oss << h << "h ";
    oss << m << "m " << s << "s";
    return oss.str();
}

static std::string fmtStop(const Vertex* v) {
    std::ostringstream oss;
    oss << "[id=" << v->getId() << " ("
        << std::fixed << std::setprecision(1)
        << v->getCoordinates().getX() << ", "
        << v->getCoordinates().getY() << ")]";
    return oss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

VRP::VRP(const Multigraph& graph, const Quadtree& quadtree)
        : graph(graph), quadtree(quadtree) {}

// ─────────────────────────────────────────────────────────────────────────────
// JSON parsing  (hand-rolled, no external library)
// ─────────────────────────────────────────────────────────────────────────────

void VRP::loadFromJSON(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open())
        throw std::runtime_error("VRP: cannot open " + filepath);

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
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r')) pos++;
        size_t end = pos;
        while (end < json.size() && (std::isdigit(json[end]) || json[end] == '.' || json[end] == '-')) end++;
        return {std::stod(json.substr(pos, end - pos)), end};
    };

    buses.clear();
    requests.clear();

    // ── Buses ─────────────────────────────────────────────────────────────────
    size_t busesStart = json.find("\"buses\"");
    size_t reqsStart  = json.find("\"requests\"");
    if (busesStart == std::string::npos || reqsStart == std::string::npos)
        throw std::runtime_error("VRP JSON: missing 'buses' or 'requests' key");

    size_t pos = busesStart;
    while (pos < reqsStart) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos || objStart >= reqsStart) break;

        BusInput bus;
        auto [id,  p1] = strVal("id",       objStart); bus.id        = id;
        auto [cap, p2] = numVal("capacity",  objStart); bus.capacity  = (int)cap;

        size_t depotStart = json.find("\"depot\"", objStart);
        if (depotStart != std::string::npos && depotStart < reqsStart) {
            auto [x,  px] = numVal("x",    depotStart); bus.depot.x   = x;
            auto [y,  py] = numVal("y",    depotStart); bus.depot.y   = y;
            auto [nm, pn] = strVal("name", depotStart); bus.depotName = nm;
        }

        if (!bus.id.empty()) buses.push_back(bus);

        // Advance past this bus object (two closing braces: depot + bus)
        size_t closeDepot = json.find('}', json.find("\"depot\"", objStart));
        pos = json.find('}', closeDepot + 1) + 1;
    }

    // ── Requests ──────────────────────────────────────────────────────────────
    size_t arrStart = json.find('[', reqsStart);
    if (arrStart == std::string::npos)
        throw std::runtime_error("VRP JSON: malformed requests array");

    pos = arrStart;
    while (true) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) break;

        RequestInput req;
        auto [id, p1] = strVal("id", objStart); req.id = id;
        if (req.id.empty()) break;

        // origin — first x/y pair after "origin"
        size_t origPos = json.find("\"origin\"", objStart);
        if (origPos == std::string::npos) break;
        auto [ox, px] = numVal("x", origPos); req.origin.x = ox;
        auto [oy, py] = numVal("y", origPos); req.origin.y = oy;

        // destination — x/y after "destination"
        size_t destPos = json.find("\"destination\"", objStart);
        if (destPos == std::string::npos) break;
        auto [dx, pdx] = numVal("x", destPos); req.destination.x = dx;
        auto [dy, pdy] = numVal("y", destPos); req.destination.y = dy;

        auto [ppl, pp] = numVal("people", objStart); req.people = (int)ppl;

        requests.push_back(req);

        // Advance past this request object (origin obj + destination obj + request obj)
        size_t closeOrig = json.find('}', origPos);
        size_t closeDest = json.find('}', destPos);
        size_t closeReq  = json.find('}', std::max(closeOrig, closeDest) + 1);
        pos = closeReq + 1;
    }

    std::cout << "  Loaded " << buses.size()    << " buses, "
              << requests.size() << " requests\n";

    // ── Feasibility pre-check ─────────────────────────────────────────────────
    int totalDemand   = 0; for (auto& r : requests) totalDemand   += r.people;
    int totalCapacity = 0; for (auto& b : buses)    totalCapacity += b.capacity;
    std::cout << "  Total demand  : " << totalDemand   << " people\n";
    std::cout << "  Total capacity: " << totalCapacity << " seats\n";
    if (totalDemand > totalCapacity)
        std::cout << "  ⚠ WARNING: demand exceeds capacity — problem may be infeasible\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Distance matrix
// ─────────────────────────────────────────────────────────────────────────────

void VRP::buildDistanceMatrix(PriorityQueueSelected pqs) {
    // Collect all unique vertices: depots + origins + destinations
    std::set<u_int> seen;
    auto addStop = [&](Vertex* v) {
        if (v && seen.insert(v->getId()).second)
            stopIndex.push_back(v);
    };

    // Resolve all coords via Quadtree
    routes.clear();
    for (auto& b : buses) {
        BusRoute route;
        route.bus         = b;
        route.depotVertex = quadtree.nearest(b.depot.x, b.depot.y);
        route.load        = 0;
        routes.push_back(route);
        addStop(route.depotVertex);
    }

    // Resolve request stops and store resolved vertices on the requests
    // We piggyback the resolved vertices into the Stop structs later during
    // greedyAssign; here we just need them in the index.
    std::vector<Vertex*> originVerts, destVerts;
    for (auto& r : requests) {
        Vertex* ov = quadtree.nearest(r.origin.x,      r.origin.y);
        Vertex* dv = quadtree.nearest(r.destination.x, r.destination.y);
        originVerts.push_back(ov);
        destVerts.push_back(dv);
        addStop(ov);
        addStop(dv);
    }

    // Build index map
    for (int i = 0; i < (int)stopIndex.size(); i++)
        vertexToIdx[stopIndex[i]->getId()] = i;

    int N = (int)stopIndex.size();
    distMatrix.assign(N, std::vector<double>(N, std::numeric_limits<double>::max()));
    for (int i = 0; i < N; i++) distMatrix[i][i] = 0.0;

    std::cout << "  Building " << N << "×" << N
              << " distance matrix (" << N*(N-1)/2 << " A* calls)...\n";

    // Run A*/Dijkstra from each stop vertex, read off all distances
    for (int i = 0; i < N; i++) {
        auto src = graph.getVertex(stopIndex[i]->getId());
        graph.dijkstra_filter_aux(src, nullptr, {BUS, WALK}, pqs);           // fills dist on every vertex

        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            auto dst = graph.getVertex(stopIndex[j]->getId());
            distMatrix[i][j] = dst->getDist();  // seconds
        }
    }

    // Store resolved vertices back for greedyAssign
    // (attach to a temporary parallel vector keyed by request index)
    // We reuse the originVerts / destVerts vectors captured above.
    // They are used inside greedyAssign via closure over this scope — so we
    // promote them to member variables via a small trick: store them inside
    // the routes' stops as pending stops before ordering.
    for (int ri = 0; ri < (int)requests.size(); ri++) {
        Stop pickup, dropoff;
        pickup.requestId  = requests[ri].id;
        pickup.vertex     = originVerts[ri];
        pickup.isPickup   = true;
        pickup.people     = requests[ri].people;

        dropoff.requestId = requests[ri].id;
        dropoff.vertex    = destVerts[ri];
        dropoff.isPickup  = false;
        dropoff.people    = requests[ri].people;

        (void)pickup; (void)dropoff;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1 — greedy assignment
// ─────────────────────────────────────────────────────────────────────────────

void VRP::greedyAssign() {
    // Sort requests largest-first so big groups get first pick of capacity
    std::vector<int> order(requests.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return requests[a].people > requests[b].people;
    });

    for (int ri : order) {
        const RequestInput& req = requests[ri];

        Vertex* ov = quadtree.nearest(req.origin.x,      req.origin.y);
        Vertex* dv = quadtree.nearest(req.destination.x, req.destination.y);

        // Find the bus that minimises: travel_time(depot→pickup→dropoff) + boarding
        // AND has enough remaining capacity
        int    bestBus  = -1;
        double bestCost = std::numeric_limits<double>::max();

        for (int bi = 0; bi < (int)routes.size(); bi++) {
            BusRoute& route = routes[bi];
            if (route.load + req.people > route.bus.capacity) continue;

            // Cost = time from last stop (or depot if empty) to pickup + pickup to dropoff
            Vertex* lastStop = route.stops.empty()
                               ? route.depotVertex
                               : route.stops.back().vertex;

            double toPickup  = edgeTime(lastStop, ov);
            double toDropoff = edgeTime(ov, dv);
            double cost      = toPickup + toDropoff;

            if (cost < bestCost) {
                bestCost = cost;
                bestBus  = bi;
            }
        }

        if (bestBus == -1) {
            std::cout << "  ✗ Request " << req.id << " (" << req.people
                      << " people) could not be assigned — no bus has capacity\n";
            continue;
        }

        BusRoute& route = routes[bestBus];
        route.stops.push_back({ req.id, ov, true,  req.people });
        route.stops.push_back({ req.id, dv, false, req.people });
        route.load += req.people;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2 — 2-opt local search
// ─────────────────────────────────────────────────────────────────────────────

void VRP::twoOpt(BusRoute& route) {
    if (route.stops.size() < 4) return;

    bool improved = true;
    while (improved) {
        improved = false;
        int n = (int)route.stops.size();
        for (int i = 0; i < n - 1; i++) {
            for (int j = i + 2; j < n; j++) {
                // Current: ...→stops[i]→stops[i+1]→...→stops[j]→stops[j+1]→...
                // Reversed segment [i+1, j]
                double before = edgeTime(route.stops[i].vertex,   route.stops[i+1].vertex)
                                + edgeTime(route.stops[j].vertex,   j+1 < n ? route.stops[j+1].vertex
                                                                            : route.depotVertex);
                double after  = edgeTime(route.stops[i].vertex,   route.stops[j].vertex)
                                + edgeTime(route.stops[i+1].vertex, j+1 < n ? route.stops[j+1].vertex
                                                                            : route.depotVertex);

                if (after < before - 1e-6) {
                    std::reverse(route.stops.begin() + i + 1,
                                 route.stops.begin() + j + 1);
                    improved = true;
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Route time calculation
// ─────────────────────────────────────────────────────────────────────────────

double VRP::edgeTime(Vertex* a, Vertex* b) const {
    if (!a || !b || a == b) return 0.0;
    int ai = vertexToIdx.count(a->getId()) ? vertexToIdx.at(a->getId()) : -1;
    int bi = vertexToIdx.count(b->getId()) ? vertexToIdx.at(b->getId()) : -1;
    if (ai == -1 || bi == -1) return std::numeric_limits<double>::max();
    return distMatrix[ai][bi];
}

double VRP::routeTime(const BusRoute& route) const {
    if (route.stops.empty()) return 0.0;
    double t = edgeTime(route.depotVertex, route.stops.front().vertex);
    for (int i = 0; i + 1 < (int)route.stops.size(); i++)
        t += edgeTime(route.stops[i].vertex, route.stops[i+1].vertex);
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// solve()
// ─────────────────────────────────────────────────────────────────────────────

bool VRP::solve(PriorityQueueSelected pqs) {
    std::cout << "\n  Phase 0 — building distance matrix...\n";
    buildDistanceMatrix(pqs);

    std::cout << "\n  Phase 1 — greedy assignment...\n";
    greedyAssign();

    std::cout << "\n  Phase 2 — 2-opt local search...\n";
    for (auto& route : routes) {
        twoOpt(route);
        route.time = routeTime(route);
    }

    makespan = 0.0;
    for (auto& r : routes) makespan = std::max(makespan, r.time);

    // Check all requests were assigned
    int assigned = 0;
    for (auto& route : routes)
        for (auto& stop : route.stops)
            if (stop.isPickup) assigned += stop.people;

    int totalDemand = 0;
    for (auto& r : requests) totalDemand += r.people;

    feasible = (assigned == totalDemand);
    return feasible;
}

// ─────────────────────────────────────────────────────────────────────────────
// printSolution()
// ─────────────────────────────────────────────────────────────────────────────

void VRP::printSolution() const {
    const std::string SEP  = "  " + std::string(62, '-') + "\n";
    const std::string SEP2 = "  " + std::string(62, '=') + "\n";

    std::cout << "\n" << SEP2;
    std::cout << "  VRP SOLUTION\n";
    std::cout << SEP2;

    if (!feasible)
        std::cout << "  ⚠  INFEASIBLE — not all requests could be assigned\n\n";

    for (const BusRoute& route : routes) {
        std::cout << SEP;
        std::cout << "  🚌 " << route.bus.id
                  << "  │  capacity=" << route.bus.capacity
                  << "  │  assigned=" << route.load
                  << "  │  time=" << fmtTime(route.time) << "\n";
        std::cout << SEP;

        if (route.stops.empty()) {
            std::cout << "    (no requests assigned to this bus)\n\n";
            continue;
        }

        int currentLoad = 0;

        // Depot
        std::cout << "    DEPOT     " << route.bus.depotName
                  << "  " << fmtStop(route.depotVertex) << "\n";

        for (int i = 0; i < (int)route.stops.size(); i++) {
            const Stop& stop = route.stops[i];

            // Travel time from previous point
            Vertex* prev = (i == 0) ? route.depotVertex : route.stops[i-1].vertex;
            double  leg  = edgeTime(prev, stop.vertex);

            std::cout << "      │\n";
            std::cout << "      │  (" << fmtTime(leg) << ")\n";
            std::cout << "      │\n";

            if (stop.isPickup) {
                currentLoad += stop.people;
                std::cout << "    ▲ PICKUP   req=" << stop.requestId
                          << "  +" << stop.people << " people boarding"
                          << "  →  on board: " << currentLoad
                          << "/" << route.bus.capacity << "\n";
            } else {
                currentLoad -= stop.people;
                std::cout << "    ▼ DROPOFF  req=" << stop.requestId
                          << "  -" << stop.people << " people alighting"
                          << "  →  on board: " << currentLoad
                          << "/" << route.bus.capacity << "\n";
            }
            std::cout << "      " << fmtStop(stop.vertex) << "\n";
        }

        std::cout << "      │\n";
        std::cout << "      │  (return to depot)\n";
        std::cout << "      │\n";
        std::cout << "    DEPOT     " << route.bus.depotName << "\n\n";
    }

    std::cout << SEP2;
    std::cout << "  MAKESPAN  :  " << fmtTime(makespan) << "\n";
    std::cout << "  FEASIBLE  :  " << (feasible ? "YES ✓" : "NO ✗") << "\n";

    // Summary table
    std::cout << "\n  Per-bus summary:\n";
    std::cout << "  " << std::left
              << std::setw(12) << "Bus"
              << std::setw(12) << "Assigned"
              << std::setw(12) << "Capacity"
              << std::setw(10) << "Util%"
              << "Time\n";
    std::cout << "  " << std::string(54, '-') << "\n";
    for (const BusRoute& r : routes) {
        double util = r.bus.capacity > 0
                      ? 100.0 * r.load / r.bus.capacity : 0.0;
        std::cout << "  " << std::left
                  << std::setw(12) << r.bus.id
                  << std::setw(12) << r.load
                  << std::setw(12) << r.bus.capacity
                  << std::setw(10) << std::fixed << std::setprecision(1) << util
                  << fmtTime(r.time) << "\n";
    }
    std::cout << SEP2;
}

// ─────────────────────────────────────────────────────────────────────────────
// exportCSV()
// Exports two files:
//   vrp_routes.csv  — edges per bus segment (bus_id, leg index, x1,y1,x2,y2)
//   vrp_stops.csv   — annotated stops       (bus_id, stop type, people, load, x, y, label)
// ─────────────────────────────────────────────────────────────────────────────

void VRP::exportCSV(const std::string& dirpath) const {
    // ── vrp_routes.csv ───────────────────────────────────────────────────────
    {
        std::ofstream out(dirpath + "/vrp_routes.csv");
        if (!out.is_open())
            throw std::runtime_error("VRP: cannot write vrp_routes.csv in " + dirpath);

        out << "bus_id,leg,x1,y1,x2,y2\n";
        out << std::fixed << std::setprecision(3);

        for (const BusRoute& route : routes) {
            if (route.stops.empty()) continue;

            auto writeEdge = [&](int leg, const Vertex* a, const Vertex* b) {
                out << route.bus.id << "," << leg << ","
                    << a->getCoordinates().getX() << ","
                    << a->getCoordinates().getY() << ","
                    << b->getCoordinates().getX() << ","
                    << b->getCoordinates().getY() << "\n";
            };

            int leg = 0;
            writeEdge(leg++, route.depotVertex, route.stops.front().vertex);
            for (int i = 0; i + 1 < (int)route.stops.size(); i++)
                writeEdge(leg++, route.stops[i].vertex, route.stops[i+1].vertex);
            // Return to depot
            writeEdge(leg, route.stops.back().vertex, route.depotVertex);
        }
    }

    // ── vrp_stops.csv ────────────────────────────────────────────────────────
    {
        std::ofstream out(dirpath + "/vrp_stops.csv");
        if (!out.is_open())
            throw std::runtime_error("VRP: cannot write vrp_stops.csv in " + dirpath);

        out << "bus_id,stop_index,type,request_id,people_delta,load_after,x,y,label\n";
        out << std::fixed << std::setprecision(3);

        for (const BusRoute& route : routes) {
            // Depot entry
            out << route.bus.id << ",0,depot,,"
                << 0 << "," << 0 << ","
                << route.depotVertex->getCoordinates().getX() << ","
                << route.depotVertex->getCoordinates().getY() << ","
                << "Depot " << route.bus.depotName << "\n";

            int load = 0;
            for (int i = 0; i < (int)route.stops.size(); i++) {
                const Stop& stop = route.stops[i];
                if (stop.isPickup) load += stop.people;
                else               load -= stop.people;

                std::string type  = stop.isPickup ? "pickup" : "dropoff";
                std::string label = (stop.isPickup ? "+" : "-")
                                    + std::to_string(stop.people)
                                    + " [" + stop.requestId + "]";

                out << route.bus.id << ","
                    << (i + 1)      << ","
                    << type         << ","
                    << stop.requestId << ","
                    << (stop.isPickup ? stop.people : -stop.people) << ","
                    << load         << ","
                    << stop.vertex->getCoordinates().getX() << ","
                    << stop.vertex->getCoordinates().getY() << ","
                    << label        << "\n";
            }

            // Depot return
            out << route.bus.id << "," << (route.stops.size() + 1)
                << ",depot_return,,"
                << 0 << "," << 0 << ","
                << route.depotVertex->getCoordinates().getX() << ","
                << route.depotVertex->getCoordinates().getY() << ","
                << "Return " << route.bus.depotName << "\n";
        }
    }

    std::cout << "  Exported → " << dirpath << "/vrp_routes.csv\n";
    std::cout << "  Exported → " << dirpath << "/vrp_stops.csv\n";
}