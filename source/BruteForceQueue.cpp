#include "../header/BruteForceQueue.h"
#include "../header/Vertex.h"

void BruteForceQueue::insert(Vertex* x) {
    H.push_back(x);
}

Vertex* BruteForceQueue::extractMin() {
    if (H.empty()) return nullptr;

    auto minIt = H.begin();
    for (auto it = H.begin() + 1; it != H.end(); ++it) {
        if (**it < **minIt) minIt = it;
    }

    Vertex* min = *minIt;
    *minIt = H.back();  // swap with last and pop — O(1) removal
    H.pop_back();
    return min;
}

void BruteForceQueue::decreaseKey(Vertex* x) {
    // The vertex is already in H with its dist updated by the caller.
    // No structural changes needed — extractMin always scans anyway.
}

bool BruteForceQueue::empty() {
    return H.empty();
}