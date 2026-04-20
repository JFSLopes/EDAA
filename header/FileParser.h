#ifndef EDAA_FILEPARSER_H
#define EDAA_FILEPARSER_H

#include "Multigraph.h"
#include <string>

class FileParser {
private:
    const std::string node_file;
    const std::string edge_file;

    static Mode parseModeString(const std::string& mode_str);

public:
    FileParser(std::string node_file, std::string edge_file);

    /**
     * @brief Parses the node and edge CSV files and populates the graph
     * @param graph The multigraph to populate
     */
    void parse(Multigraph& graph) const;
};

#endif //EDAA_FILEPARSER_H