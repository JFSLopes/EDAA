#ifndef EDAA_BRUTEFORCEQUEUE_H
#define EDAA_BRUTEFORCEQUEUE_H

#include "PriorityQueue.h"
#include <vector>

class BruteForceQueue : public PriorityQueue {
    std::vector<Vertex*> H;
public:
    void insert(Vertex* x) override;
    Vertex* extractMin() override;
    void decreaseKey(Vertex* x) override;
    bool empty() override;
};

#endif //EDAA_BRUTEFORCEQUEUE_H