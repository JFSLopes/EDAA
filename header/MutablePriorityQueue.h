#ifndef EDAA_MUTABLEPRIORITYQUEUE_H
#define EDAA_MUTABLEPRIORITYQUEUE_H

#include "PriorityQueue.h"
#include <vector>

class Vertex;

class MutablePriorityQueue : public PriorityQueue {
    std::vector<Vertex*> H;
    void heapifyUp(unsigned i);
    void heapifyDown(unsigned i);
    void set(unsigned i, Vertex* x);
public:
    MutablePriorityQueue();
    void insert(Vertex* x) override;
    Vertex* extractMin() override;
    void decreaseKey(Vertex* x) override;
    bool empty() override;
};

#endif //EDAA_MUTABLEPRIORITYQUEUE_H
