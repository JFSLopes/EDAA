#ifndef EDAA_PRIORITYQUEUE_H
#define EDAA_PRIORITYQUEUE_H

class Vertex;

class PriorityQueue {
public:
    virtual ~PriorityQueue() = default;
    virtual void insert(Vertex* x) = 0;
    virtual Vertex* extractMin() = 0;
    virtual void decreaseKey(Vertex* x) = 0;
    virtual bool empty() = 0;
};

#endif //EDAA_PRIORITYQUEUE_H
