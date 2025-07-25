#ifndef DUMMY_ADAPTER_H
#define DUMMY_ADAPTER_H

#include <iostream>
#include "errors.h"
#include "random_fnv1a.h"
#include "record_manager.h"
#ifdef USE_TREE_STATS
#   include "tree_stats.h"
#endif

#define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, int>

template <typename K, typename V, class Reclaim = reclaimer_debra<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
private:
    RECORD_MANAGER_T * const recmgr;
    const V NO_VALUE;
public:
    ds_adapter(const int NUM_THREADS,
               const K& unused1,
               const K& unused2,
               const V& unused3,
               RandomFNV1A * const unused4)
    : recmgr(new RECORD_MANAGER_T(NUM_THREADS, SIGQUIT))
    , NO_VALUE(unused3)
    {}
    ~ds_adapter() {
        delete recmgr;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void initThread(const int tid) {
        recmgr->initThread(tid);
    }
    void deinitThread(const int tid) {
        recmgr->deinitThread(tid);
    }

    bool contains(const int tid, const K& key) {
        return false;
    }
    V insert(const int tid, const K& key, const V& val) {
        return val; // fail
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        return val; // fail
    }
    V erase(const int tid, const K& key) {
        return NO_VALUE; // fail
    }
    V find(const int tid, const K& key) {
        return NO_VALUE;
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        return 0;
    }
    void printSummary() {
        recmgr->printStatus();
    }

    long long getKeySum() {
        return 0;
    }

    //used only for lists types not trees
    long long getDSSize() {
        return 0;
    }

    bool validateStructure() {
        return true;//ds->validate();
    }

    bool isTree()
    {
        return false;
    }

    void printObjectSizes() {
    }
    void debugGCSingleThreaded() {}

#ifdef USE_TREE_STATS
    class NodeHandler {
    public:
        typedef int * NodePtrType;

        NodeHandler(const K& _minKey, const K& _maxKey) {}

        class ChildIterator {
        public:
            ChildIterator(NodePtrType _node) {}
            bool hasNext() {
                return false;
            }
            NodePtrType next() {
                return NULL;
            }
        };

        bool isLeaf(NodePtrType node) {
            return false;
        }
        size_t getNumChildren(NodePtrType node) {
            return 0;
        }
        size_t getNumKeys(NodePtrType node) {
            return 0;
        }
        size_t getSumOfKeys(NodePtrType node) {
            return 0;
        }
        ChildIterator getChildIterator(NodePtrType node) {
            return ChildIterator(node);
        }
    };
    TreeStats<NodeHandler> * createTreeStats(const K& _minKey, const K& _maxKey) {
        return new TreeStats<NodeHandler>(new NodeHandler(_minKey, _maxKey), NULL, true);
    }
#endif
};

#endif
