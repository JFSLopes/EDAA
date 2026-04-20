#include "../header/Multigraph.h"
#include <cfloat>
#include <fstream>

Multigraph::Multigraph() : vertexSet(std::vector<std::shared_ptr<Vertex>>()) {}

std::vector<std::shared_ptr<Vertex>>& Multigraph::getVertexSet() {
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

std::vector<std::shared_ptr<Vertex>> Multigraph::prim(const std::shared_ptr<Vertex>& s) const {
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

    MutablePriorityQueue<Vertex> q;
    Vertex* start = s.get();
    start->setDist(0);
    q.insert(start);
    while (!q.empty()) {
        /// The vertex on the top is always relaxed, that is why we can add it to the answer.
        Vertex* minVertex = q.extractMin();
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
                q.insert(dest);
            }
                /// If it was already added to the queue, but we found a better path, we update it.
            else if (edge->getWeight() < dest->getDist()){
                edge->setSelected(true); /// Used to create the traversal tree
                dest->getPath()->setSelected(false);
                dest->setPath(edge);
                dest->setDist(edge->getWeight());
                q.decreaseKey(dest);
            }
        }
    }
    return ans;
}

void Multigraph::exportPrimCSV(const std::vector<std::shared_ptr<Vertex>>& ans, const std::string& filepath) const {
    std::ofstream out(filepath);
    out << "x1,y1,x2,y2,mode\n";
    for (const std::shared_ptr<Vertex>& v : ans) {
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