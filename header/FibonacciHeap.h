#ifndef EDAA_FIBONACCIHEAP_H
#define EDAA_FIBONACCIHEAP_H

#include "PriorityQueue.h"
#include <unordered_map>
#include <vector>
#include <cmath>

class Vertex;

class FibonacciHeap : public PriorityQueue {
    struct FibNode {
        Vertex* vertex;
        FibNode* parent   = nullptr;
        FibNode* child    = nullptr;
        FibNode* left     = nullptr;
        FibNode* right    = nullptr;
        int      degree   = 0;
        bool     marked   = false;
        explicit FibNode(Vertex* v) : vertex(v), left(this), right(this) {}
    };

    FibNode* minNode = nullptr;
    int      size_   = 0;
    std::unordered_map<Vertex*, FibNode*> nodeMap; // to find a node by vertex pointer

    void link(FibNode* child, FibNode* parent);
    void consolidate();
    void cut(FibNode* x, FibNode* y);
    void cascadingCut(FibNode* y);
    void addToRootList(FibNode* x);
    void removeFromRootList(FibNode* x);

public:
    ~FibonacciHeap() override;
    void insert(Vertex* x) override;
    Vertex* extractMin() override;
    void decreaseKey(Vertex* x) override;
    bool empty() override;
};

#endif //EDAA_FIBONACCIHEAP_H