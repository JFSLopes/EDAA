#include "../header/FibonacciHeap.h"
#include "../header/Vertex.h"

FibonacciHeap::~FibonacciHeap() {
    // BFS over all nodes to delete them
    std::vector<FibNode*> toDelete;
    if (!minNode) return;

    std::vector<FibNode*> roots;
    FibNode* cur = minNode;
    do { roots.push_back(cur); cur = cur->right; } while (cur != minNode);

    while (!roots.empty()) {
        FibNode* node = roots.back(); roots.pop_back();
        if (node->child) {
            FibNode* c = node->child;
            do { roots.push_back(c); c = c->right; } while (c != node->child);
        }
        delete node;
    }
}

void FibonacciHeap::addToRootList(FibNode* x) {
    x->parent = nullptr;
    if (!minNode) {
        x->left = x->right = x;
        minNode = x;
    } else {
        // Insert x to the left of minNode in the circular list
        x->right = minNode;
        x->left  = minNode->left;
        minNode->left->right = x;
        minNode->left = x;
        if (x->vertex->getDist() < minNode->vertex->getDist())
            minNode = x;
    }
}

void FibonacciHeap::removeFromRootList(FibNode* x) {
    x->left->right = x->right;
    x->right->left = x->left;
}

void FibonacciHeap::insert(Vertex* v) {
    FibNode* node = new FibNode(v);
    nodeMap[v] = node;
    addToRootList(node);
    size_++;
}

Vertex* FibonacciHeap::extractMin() {
    FibNode* z = minNode;
    if (!z) return nullptr;

    // Promote all children to a root list
    if (z->child) {
        std::vector<FibNode*> children;
        FibNode* c = z->child;
        do { children.push_back(c); c = c->right; } while (c != z->child);
        for (FibNode* child : children) {
            addToRootList(child);
            child->parent = nullptr;
        }
    }

    removeFromRootList(z);
    if (z == z->right) {
        minNode = nullptr;   // the heap is now empty
    } else {
        minNode = z->right;
        consolidate();
    }

    size_--;
    Vertex* result = z->vertex;
    nodeMap.erase(result);
    delete z;
    return result;
}

void FibonacciHeap::link(FibNode* y, FibNode* x) {
    // Make y a child of x
    removeFromRootList(y);
    y->parent = x;
    if (!x->child) {
        x->child = y;
        y->left = y->right = y;
    } else {
        y->right = x->child;
        y->left  = x->child->left;
        x->child->left->right = y;
        x->child->left = y;
    }
    x->degree++;
    y->marked = false;
}

void FibonacciHeap::consolidate() {
    int maxDeg = static_cast<int>(std::log2(size_)) + 2;
    std::vector<FibNode*> degTable(maxDeg, nullptr);

    // Collect all roots first to avoid iterator invalidation
    std::vector<FibNode*> roots;
    FibNode* cur = minNode;
    do { roots.push_back(cur); cur = cur->right; } while (cur != minNode);

    for (FibNode* w : roots) {
        FibNode* x = w;
        int d = x->degree;
        while (d < maxDeg && degTable[d]) {
            FibNode* y = degTable[d];
            if (x->vertex->getDist() > y->vertex->getDist()) std::swap(x, y);
            link(y, x);
            degTable[d] = nullptr;
            d++;
        }
        if (d < maxDeg) degTable[d] = x;
    }

    // Rebuild root list and find new min
    minNode = nullptr;
    for (FibNode* node : degTable) {
        if (!node) continue;
        node->left = node->right = node;  // isolate before re-adding
        addToRootList(node);
    }
}

void FibonacciHeap::cut(FibNode* x, FibNode* y) {
    // Remove x from child list of y
    if (x->right == x) {
        y->child = nullptr;
    } else {
        if (y->child == x) y->child = x->right;
        x->left->right = x->right;
        x->right->left = x->left;
    }
    y->degree--;
    addToRootList(x);
    x->marked = false;
}

void FibonacciHeap::cascadingCut(FibNode* y) {
    FibNode* z = y->parent;
    if (z) {
        if (!y->marked) y->marked = true;
        else { cut(y, z); cascadingCut(z); }
    }
}

void FibonacciHeap::decreaseKey(Vertex* v) {
    FibNode* x = nodeMap[v];
    FibNode* y = x->parent;
    if (y && x->vertex->getDist() < y->vertex->getDist()) {
        cut(x, y);
        cascadingCut(y);
    }
    if (v->getDist() < minNode->vertex->getDist())
        minNode = x;
}

bool FibonacciHeap::empty() {
    return size_ == 0;
}