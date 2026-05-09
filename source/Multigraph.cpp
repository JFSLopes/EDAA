#include "../header/Multigraph.h"
#include <cfloat>
#include <algorithm>
#include <fstream>

Multigraph::Multigraph() : vertexSet(std::vector<std::shared_ptr<Vertex>>()) {}

const std::vector<std::shared_ptr<Vertex>>& Multigraph::getVertexSet() const{
    return vertexSet;
}

std::shared_ptr<Vertex> Multigraph::getVertex(u_int id) const {
    return id < vertexSet.size() ? vertexSet[id] : nullptr;
}

u_int Multigraph::addVertex(double x, double y, const std::string &name) {
    u_int id = vertexSet.size();
    vertexSet.emplace_back(std::make_shared<Vertex>(id ,x, y, name));
    return id;    // index
}

void Multigraph::addEdge(u_int source_id, u_int target_id, double weight, Mode mode) const {
    std::shared_ptr<Vertex> source = getVertex(source_id);
    std::shared_ptr<Vertex> dest = getVertex(target_id);

    source->addEdge(source, dest, weight, mode);
    dest->addEdge(dest, source, weight, mode);
}

std::vector<std::shared_ptr<Vertex>> Multigraph::prim(const std::shared_ptr<Vertex>& s, PriorityQueueSelected pqs) const {
    std::vector<std::shared_ptr<Vertex>> ans;
    /// Init the values
    for (const std::shared_ptr<Vertex>& v: vertexSet){
        v->setDist(DBL_MAX);
        v->setVisited(false);
        v->setPath(nullptr);
        for (const std::shared_ptr<Edge>& e : v->getAdj()){
            e->setSelected(false);
        }
    }

    std::unique_ptr<PriorityQueue> q = makePQ(pqs);

    Vertex* start = s.get();
    start->setDist(0);
    q->insert(start);
    while (!q->empty()) {
        /// The vertex on the top is always relaxed, that is why we can add it to the answer.
        Vertex* minVertex = q->extractMin();
        minVertex->setVisited(true);
        ans.push_back(getVertex(minVertex->getId()));

        for (const std::shared_ptr<Edge>& edge : minVertex->getAdj()){
            Vertex* dest = edge->getDest().get();
            /// If it was already visited (means added to the mst) we don't do anything else
            if (dest->isVisited()) continue;
                /// If it was not yet processed, we just change its dist and add it to the queue
            else if (dest->getDist() == DBL_MAX){
                edge->setSelected(true);
                dest->setPath(edge);
                dest->setDist(edge->getWeight());
                q->insert(dest);
            }
                /// If it was already added to the queue, but we found a better path, we update it.
            else if (edge->getWeight() < dest->getDist()){
                edge->setSelected(true); /// Used to create the traversal tree
                dest->getPath()->setSelected(false);
                dest->setPath(edge);
                dest->setDist(edge->getWeight());
                q->decreaseKey(dest);
            }
        }
    }
    return ans;
}

void Multigraph::exportPathCSV(const std::vector<std::shared_ptr<Vertex>>& path, const std::string& filepath) const {
    std::ofstream out(filepath);
    out << "x1,y1,x2,y2,mode\n";
    for (const std::shared_ptr<Vertex>& v : path) {
        auto edge = v->getPath();
        if (edge == nullptr) continue;

        std::string mode_str;
        switch (edge->getMode()) {
            case WALK:  mode_str = "walk";  break;
            case BUS:   mode_str = "bus";   break;
            case METRO: mode_str = "metro"; break;
        }

        out << v->getCoordinates().getX()                    << ","
            << v->getCoordinates().getY()                    << ","
            << edge->getOrigin()->getCoordinates().getX()    << ","
            << edge->getOrigin()->getCoordinates().getY()    << ","
            << mode_str                                      << "\n";
    }
}

std::vector<std::shared_ptr<Vertex>> Multigraph::dijkstra(const std::shared_ptr<Vertex> &src, const std::shared_ptr<Vertex> &dest, PriorityQueueSelected pqs) const {
    /// Run Dijkstra to get the smaller distances
    this->dijkstra_aux(src, pqs);

    std::vector<std::shared_ptr<Vertex>> ans;

    /// Building from dest to src is easier because there can only be a single path
    std::shared_ptr<Vertex> current = dest;
    while (current && current != src) {
        ans.push_back(current->getPath()->getOrigin());
        current = current->getPath()->getOrigin();
    }

    std::reverse(ans.begin(), ans.end());
    return ans;
}

void Multigraph::dijkstra_aux(const std::shared_ptr<Vertex>& src, PriorityQueueSelected pqs) const {
    std::vector<std::shared_ptr<Vertex>> ans;
    /// Init the values
    for (const std::shared_ptr<Vertex>& v: vertexSet){
        v->setDist(DBL_MAX);
        v->setVisited(false);
        v->setPath(nullptr);
    }

    src->setDist(0);

    std::unique_ptr<PriorityQueue> q = makePQ(pqs);

    q->insert(src.get());
    while (!q->empty()) {
        /// The vertex on the top is always relaxed, that is why we can add it to the answer.
        Vertex* min = q->extractMin();
        min->setVisited(true);

        for (const std::shared_ptr<Edge>& edge : min->getAdj()){
            Vertex* dest = edge->getDest().get();
            if (dest->isVisited()) continue; /// It was already relaxed
            else if (dest->getDist() == DBL_MAX){ /// Never relaxed
                dest->setPath(edge);
                dest->setDist(edge->getOrigin()->getDist() + edge->getWeight());
                q->insert(dest);
            }
            else if (edge->getOrigin()->getDist() + edge->getWeight() < dest->getDist()){
                /// It is already added to the queue, but we found a better path, so we update it.
                dest->setPath(edge);
                dest->setDist(edge->getOrigin()->getDist() + edge->getWeight());
                q->decreaseKey(dest);
            }
        }
    }
}


std::vector<std::shared_ptr<Vertex>> Multigraph::dijkstra_filter(const std::shared_ptr<Vertex> &src, const std::shared_ptr<Vertex> &dest, const std::set<Mode>& modes, PriorityQueueSelected pqs) const {
    /// Run Dijkstra to get the smaller distances
    this->dijkstra_filter_aux(src, modes, pqs);

    std::vector<std::shared_ptr<Vertex>> ans;

    /// Building from dest to src is easier because there can only be a single path
    std::shared_ptr<Vertex> current = dest;
    while (current && current != src) {
        ans.push_back(current->getPath()->getOrigin());
        current = current->getPath()->getOrigin();
    }

    std::reverse(ans.begin(), ans.end());
    return ans;
}

void Multigraph::dijkstra_filter_aux(const std::shared_ptr<Vertex>& src, const std::set<Mode>& modes, PriorityQueueSelected pqs) const {
    std::vector<std::shared_ptr<Vertex>> ans;
    /// Init the values
    for (const std::shared_ptr<Vertex>& v: vertexSet){
        v->setDist(DBL_MAX);
        v->setVisited(false);
        v->setPath(nullptr);
    }

    src->setDist(0);

    std::unique_ptr<PriorityQueue> q = makePQ(pqs);

    q->insert(src.get());
    while (!q->empty()) {
        /// The vertex on the top is always relaxed, that is why we can add it to the answer.
        Vertex* min = q->extractMin();
        min->setVisited(true);

        for (const std::shared_ptr<Edge>& edge : min->getAdj()){
            if (modes.find(edge->getMode()) == modes.end()){    /// Cannot use this edge
                continue;
            }
            Vertex* dest = edge->getDest().get();
            if (dest->isVisited()) continue; /// It was already relaxed
            else if (dest->getDist() == DBL_MAX){ /// Never relaxed
                dest->setPath(edge);
                dest->setDist(edge->getOrigin()->getDist() + edge->getWeight());
                q->insert(dest);
            }
            else if (edge->getOrigin()->getDist() + edge->getWeight() < dest->getDist()){
                /// It is already added to the queue, but we found a better path, so we update it.
                dest->setPath(edge);
                dest->setDist(edge->getOrigin()->getDist() + edge->getWeight());
                q->decreaseKey(dest);
            }
        }
    }
}


std::vector<std::shared_ptr<Vertex>> Multigraph::astar(const std::shared_ptr<Vertex>& src, const std::shared_ptr<Vertex>& dest, PriorityQueueSelected pqs) const {
    this->astar_aux(src, dest, pqs);

    std::vector<std::shared_ptr<Vertex>> ans;
    std::shared_ptr<Vertex> current = dest;
    while (current && current != src) {
        ans.push_back(current->getPath()->getOrigin());
        current = current->getPath()->getOrigin();
    }
    std::reverse(ans.begin(), ans.end());
    return ans;
}

void Multigraph::astar_aux(const std::shared_ptr<Vertex>& src, const std::shared_ptr<Vertex>& dest, PriorityQueueSelected pqs) const {
    /// dist stores f = g + h for queue ordering
    /// gCost stores the true cost from src
    std::unordered_map<u_int, double> gCost;

    for (const std::shared_ptr<Vertex>& v : vertexSet) {
        v->setDist(DBL_MAX);
        v->setVisited(false);
        v->setPath(nullptr);
        gCost[v->getId()] = DBL_MAX;
    }

    /**
     * Uses SPEED_METRO (fastest mode) as the divisor — any edge, regardless of mode,
     * takes at least dist/SPEED_METRO seconds, so this never overestimates the true cost.
     * The 0.99 safety factor absorbs floating-point imprecision introduced by the
     * node normalisation (centroid merging) in the Python pipeline, where ~34k walk
     * edges end up with euclidean > actual by small amounts (<1e-2s)
     */
    auto heuristic = [](const Vertex* a, const Vertex* b) -> double {
        return ADMISSIBILITY_SAFETY
               * a->getCoordinates().distanceTo(b->getCoordinates())
               / SPEED_METRO;
    };

    std::unique_ptr<PriorityQueue> q = makePQ(pqs);
    gCost[src->getId()] = 0.0;
    src->setDist(heuristic(src.get(), dest.get())); // f = 0 + h
    q->insert(src.get());

    while (!q->empty()) {
        Vertex* current = q->extractMin();

        /// Early exit — no need to explore the whole graph
        if (current == dest.get()) break;

        current->setVisited(true);

        for (const std::shared_ptr<Edge>& edge : current->getAdj()) {
            Vertex* nb = edge->getDest().get();
            if (nb->isVisited()) continue;

            double tentativeG = gCost[current->getId()] + edge->getWeight();
            if (tentativeG < gCost[nb->getId()]) {
                gCost[nb->getId()] = tentativeG;
                double f = tentativeG + heuristic(nb, dest.get());

                bool wasInQueue = nb->getDist() != DBL_MAX;
                nb->setPath(edge);
                nb->setDist(f);

                if (!wasInQueue) q->insert(nb);
                else             q->decreaseKey(nb);
            }
        }
    }

    /// Write true g costs back into dist so dest->getDist() returns the real distance
    for (const std::shared_ptr<Vertex>& v : vertexSet)
        v->setDist(gCost[v->getId()]);
}