#ifndef EDAA_FIBONACCIHEAP_H
#define EDAA_FIBONACCIHEAP_H

#include "PriorityQueue.h"
#include <unordered_map>
#include <vector>
#include <cmath>

class Vertex;

class FibonacciHeap : public PriorityQueue {
    struct FibNode {
        Vertex*  vertex;
        FibNode* prev    = nullptr;
        FibNode* next    = nullptr;
        FibNode* child   = nullptr;
        FibNode* parent  = nullptr;
        int      degree  = 0;
        bool     marked  = false;
        explicit FibNode(Vertex* v) : vertex(v) { prev = next = this; }
    };

    FibNode* heap_ = nullptr;
    int      size_ = 0;
    std::unordered_map<Vertex*, FibNode*> nodeMap;

    FibNode* _merge(FibNode* a, FibNode* b);
    void     _deleteAll(FibNode* n);
    void     _addChild(FibNode* parent, FibNode* child);
    void     _unMarkAndUnParentAll(FibNode* n);
    FibNode* _removeMinimum(FibNode* n);
    FibNode* _cut(FibNode* heap, FibNode* n);
    FibNode* _decreaseKey(FibNode* heap, FibNode* n);

public:
    ~FibonacciHeap() override;
    void    insert(Vertex* x) override;
    Vertex* extractMin() override;
    void    decreaseKey(Vertex* x) override;
    bool    empty() override;
};

#endif //EDAA_FIBONACCIHEAP_H