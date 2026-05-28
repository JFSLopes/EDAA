#include "../header/FibonacciHeap.h"
#include "../header/Vertex.h"

FibonacciHeap::~FibonacciHeap() {
    _deleteAll(heap_);
}

void FibonacciHeap::_deleteAll(FibNode* n) {
    if (!n) return;
    FibNode* c = n;
    do {
        FibNode* d = c;
        c = c->next;
        _deleteAll(d->child);
        delete d;
    } while (c != n);
}

FibonacciHeap::FibNode* FibonacciHeap::_merge(FibNode* a, FibNode* b) {
    if (!a) return b;
    if (!b) return a;
    /// Ensure a has the smaller dist
    if (a->vertex->getDist() > b->vertex->getDist()) std::swap(a, b);
    FibNode* an = a->next;
    FibNode* bp = b->prev;
    a->next  = b;
    b->prev  = a;
    an->prev = bp;
    bp->next = an;
    return a;
}

void FibonacciHeap::_addChild(FibNode* parent, FibNode* child) {
    child->prev = child->next = child;
    child->parent = parent;
    parent->degree++;
    parent->child = _merge(parent->child, child);
}

void FibonacciHeap::_unMarkAndUnParentAll(FibNode* n) {
    if (!n) return;
    FibNode* c = n;
    do {
        c->marked = false;
        c->parent = nullptr;
        c = c->next;
    } while (c != n);
}

FibonacciHeap::FibNode* FibonacciHeap::_removeMinimum(FibNode* n) {
    _unMarkAndUnParentAll(n->child);
    if (n->next == n) {
        n = n->child;
    } else {
        n->next->prev = n->prev;
        n->prev->next = n->next;
        n = _merge(n->next, n->child);
    }
    if (!n) return nullptr;

    FibNode* trees[64] = {nullptr};
    while (true) {
        if (trees[n->degree]) {
            FibNode* t = trees[n->degree];
            if (t == n) break;
            trees[n->degree] = nullptr;
            if (n->vertex->getDist() < t->vertex->getDist()) {
                t->prev->next = t->next;
                t->next->prev = t->prev;
                _addChild(n, t);
            } else {
                t->prev->next = t->next;
                t->next->prev = t->prev;
                if (n->next == n) {
                    t->next = t->prev = t;
                    _addChild(t, n);
                    n = t;
                } else {
                    n->prev->next = t;
                    n->next->prev = t;
                    t->next = n->next;
                    t->prev = n->prev;
                    _addChild(t, n);
                    n = t;
                }
            }
            continue;
        }
        trees[n->degree] = n;
        n = n->next;
    }

    FibNode* min   = n;
    FibNode* start = n;
    do {
        if (n->vertex->getDist() < min->vertex->getDist()) min = n;
        n = n->next;
    } while (n != start);
    return min;
}

FibonacciHeap::FibNode* FibonacciHeap::_cut(FibNode* heap, FibNode* n) {
    if (n->next == n) {
        n->parent->child = nullptr;
    } else {
        n->next->prev = n->prev;
        n->prev->next = n->next;
        n->parent->child = n->next;
    }
    n->next = n->prev = n;
    n->marked = false;
    return _merge(heap, n);
}

FibonacciHeap::FibNode* FibonacciHeap::_decreaseKey(FibNode* heap, FibNode* n) {
    if (n->parent && n->vertex->getDist() < n->parent->vertex->getDist()) {
        heap = _cut(heap, n);
        FibNode* parent = n->parent;
        n->parent = nullptr;
        while (parent && parent->marked) {
            heap = _cut(heap, parent);
            FibNode* next = parent->parent;
            parent->parent = nullptr;
            parent = next;
        }
        if (parent && parent->parent) parent->marked = true;
    } else if (!n->parent && n->vertex->getDist() < heap->vertex->getDist()) {
        heap = n;
    }
    return heap;
}

void FibonacciHeap::insert(Vertex* v) {
    countInsert();

    FibNode* n = new FibNode(v);
    nodeMap[v] = n;
    heap_ = _merge(heap_, n);
    size_++;
}

Vertex* FibonacciHeap::extractMin() {
    if (!heap_) return nullptr;
    countDelete();

    FibNode* old = heap_;
    heap_ = _removeMinimum(heap_);
    Vertex* result = old->vertex;
    nodeMap.erase(result);
    delete old;
    size_--;
    return result;
}

void FibonacciHeap::decreaseKey(Vertex* v) {
    countUpdateKey();

    /// Vertex dist was already updated by the caller (Dijkstra/Prim)
    FibNode* n = nodeMap[v];
    heap_ = _decreaseKey(heap_, n);
}

bool FibonacciHeap::empty() {
    return heap_ == nullptr;
}