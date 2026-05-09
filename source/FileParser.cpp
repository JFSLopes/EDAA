#include "../header/FileParser.h"

#include <fstream>
#include <sstream>

FileParser::FileParser(std::string node_file, std::string edge_file)
        : node_file(std::move(node_file)), edge_file(std::move(edge_file)) {}

Mode FileParser::parseModeString(const std::string& mode_str) {
    if (mode_str == "walk")  return WALK;
    if (mode_str == "bus")   return BUS;
    if (mode_str == "metro") return METRO;
    return WALK; // transfer edges default to walk
}


void FileParser::parse(Multigraph& graph) const {
    std::unordered_map<std::string, u_int> idToIndex;

    // Nodes
    std::ifstream nf(node_file);
    std::string line;
    std::getline(nf, line); // skip header

    while (std::getline(nf, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string id, x_str, y_str, name, layers;
        std::getline(ss, id,     ',');
        std::getline(ss, x_str,  ',');
        std::getline(ss, y_str,  ',');
        std::getline(ss, name,   ',');
        std::getline(ss, layers, ',');

        int index = graph.addVertex(std::stod(x_str), std::stod(y_str), name);
        idToIndex[id] = index;
    }

    // Edges
    std::ifstream ef(edge_file);
    std::getline(ef, line); // skip header
    while (std::getline(ef, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string source, target, mode_str, weight_str, weight_m_str;
        std::getline(ss, source,       ',');
        std::getline(ss, target,       ',');
        std::getline(ss, mode_str,     ',');
        std::getline(ss, weight_str,   ',');
        std::getline(ss, weight_m_str, ',');

        graph.addEdge(idToIndex.at(source), idToIndex.at(target),
                      std::stod(weight_str), parseModeString(mode_str));
    }
}