#ifndef EDAA_PRIORITYQUEUE_H
#define EDAA_PRIORITYQUEUE_H

#include <cstdint>

class Vertex;

struct PriorityQueueStats {
    std::uint64_t inserts = 0;
    std::uint64_t deletes = 0;      // successful extractMin calls
    std::uint64_t updateKeys = 0;   // decreaseKey calls

    void clear() {
        inserts = 0;
        deletes = 0;
        updateKeys = 0;
    }

    PriorityQueueStats& operator+=(const PriorityQueueStats& other) {
        inserts += other.inserts;
        deletes += other.deletes;
        updateKeys += other.updateKeys;
        return *this;
    }

    PriorityQueueStats operator/(std::uint64_t divisor) const {
        if (divisor == 0) return {};
        return {
                inserts / divisor,
                deletes / divisor,
                updateKeys / divisor
        };
    }
};

class PriorityQueue {
private:
    inline static PriorityQueueStats globalStats{};

protected:
    static void countInsert() {
        globalStats.inserts++;
    }

    static void countDelete() {
        globalStats.deletes++;
    }

    static void countUpdateKey() {
        globalStats.updateKeys++;
    }

public:
    virtual ~PriorityQueue() = default;

    static void resetStats() {
        globalStats.clear();
    }

    static PriorityQueueStats getStats() {
        return globalStats;
    }

    virtual void insert(Vertex* x) = 0;
    virtual Vertex* extractMin() = 0;
    virtual void decreaseKey(Vertex* x) = 0;
    virtual bool empty() = 0;
};

#endif //EDAA_PRIORITYQUEUE_H