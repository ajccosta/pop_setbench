/**
 * Implementation of the ASCYLIB ticket lock of David, Guerraoui and Trigonakis.
 * This is a heavily modified version of the ASCYLIB implementation.
 * (See copyright notice in ticket.h)
 * The modifications are copyrighted (consistent with the original license)
 *   by Maya Arbel-Raviv and Trevor Brown, 2018.
 */

#ifndef BST_ADAPTER_H
#define BST_ADAPTER_H

#include <iostream>
#include <csignal>
#include "errors.h"
#include "random_fnv1a.h"
#ifdef USE_TREE_STATS
#   define TREE_STATS_BYTES_AT_DEPTH
#   include "tree_stats.h"
#endif

#if defined (OOI_RECLAIMERS) || defined (OOI_POP_RECLAIMERS)
    #include "ticket_ooi_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T ticketOOI<K, V, RECORD_MANAGER_T>
#elif NZB_RECLAIMERS
    #include "ticket_nzb_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T ticketNZB<K, V, RECORD_MANAGER_T>
#elif HP_RECLAIMERS
    #include "ticket_hp_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T ticketHP<K, V, RECORD_MANAGER_T>
#elif defined(IBR_HP_RECLAIMERS) || defined (IBR_HP_POP_RECLAIMERS)
    #include "ticket_ibr_hp_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T ticketIBRHP<K, V, RECORD_MANAGER_T>
#elif defined (DAOI_RECLAIMERS) || defined (DAOI_POP_RECLAIMERS)
    #include "ticket_daoi_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T ticketDAOI<K, V, RECORD_MANAGER_T>
#elif DAOI_RUSLON_RECLAIMERS
    #include "ticket_daoi_ruslon_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T ticketDAOIRUSLON<K, V, RECORD_MANAGER_T>
// #elif OOI_POP_RECLAIMERS
//     #include "ticket_ooi_nzb_impl.h"
//     #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
//     #define DATA_STRUCTURE_T ticketOOINZB<K, V, RECORD_MANAGER_T>
#elif IBR_RCU_HP_POP_RECLAIMERS
    #include "ticket_ibr_rcuhppop_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T ticketIBRRCUHPPOP<K, V, RECORD_MANAGER_T>
#else // legacy way: all reclaimers in same ds impl 
    // #error "no ds impl"
    #include "ticket_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T ticket<K, V, RECORD_MANAGER_T>
#endif

// #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
// #define DATA_STRUCTURE_T ticket<K, V, RECORD_MANAGER_T>

template <typename K, typename V, class Reclaim = reclaimer_debra<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
private:
    const V NO_VALUE;
    DATA_STRUCTURE_T * const ds;

public:
    ds_adapter(const int NUM_THREADS,
               const K& KEY_MIN,
               const K& KEY_MAX,
               const V& VALUE_RESERVED,
               RandomFNV1A * const unused2)
    : NO_VALUE(VALUE_RESERVED)
    , ds(new DATA_STRUCTURE_T(NUM_THREADS, KEY_MIN, KEY_MAX, NO_VALUE, 0 /* unused */))
    {}
    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void initThread(const int tid) {
        ds->initThread(tid);
    }
    void deinitThread(const int tid) {
        ds->deinitThread(tid);
    }

    V insert(const int tid, const K& key, const V& val) {
        setbench_error("insert-replace functionality not implemented for this data structure");
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        return ds->bst_tk_insert(tid, key, val);
    }
    V erase(const int tid, const K& key) {
        return ds->bst_tk_delete(tid, key);
    }
    V find(const int tid, const K& key) {
        return ds->bst_tk_find(tid, key);
    }
    bool contains(const int tid, const K& key) {
        return find(tid, key) != getNoValue();
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        setbench_error("not implemented");
    }
    void printSummary() {
        auto recmgr = ds->debugGetRecMgr();
        recmgr->printStatus();
    }
    bool validateStructure() {
        return true;
    }

    //used only for lists types not trees
    long long getDSSize() {
        assert(0 && "should not use this function to cal ds size in Trees.");
        return -1;
    }

    
    /* to decide in main.cpp whether its tree validation or list validation */
    bool isTree()
    {
        return true;
    }

    /*to avoid template errors in main.cpp since tyree and list are validated separately*/
    long long getKeySum() {
        assert (0 && "for tree should not use this for key sum validation. Declared thi sto avoid template errors." );
        return 0;
    }

    void printObjectSizes() {
        std::cout<<"sizes: node="
                 <<(sizeof(node_t<K,V>))
                 <<std::endl;
    }
    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() {
        ds->debugGetRecMgr()->debugGCSingleThreaded();
    }

#ifdef USE_TREE_STATS
    class NodeHandler {
    public:
        typedef node_t<K,V> * NodePtrType;
        K minKey;
        K maxKey;

        NodeHandler(const K& _minKey, const K& _maxKey) {
            minKey = _minKey;
            maxKey = _maxKey;
        }

        class ChildIterator {
        private:
            bool leftDone;
            bool rightDone;
            NodePtrType node; // node being iterated over
        public:
            ChildIterator(NodePtrType _node) {
                node = _node;
                leftDone = (node->left == NULL);
                rightDone = (node->right == NULL);
            }
            bool hasNext() {
                return !(leftDone && rightDone);
            }
            NodePtrType next() {
                if (!leftDone) {
                    leftDone = true;
                    return node->left;
                }
                if (!rightDone) {
                    rightDone = true;
                    return node->right;
                }
                setbench_error("ERROR: it is suspected that you are calling ChildIterator::next() without first verifying that it hasNext()");
            }
        };

        bool isLeaf(NodePtrType node) {
            return (node->left == NULL) && (node->right == NULL);
        }
        size_t getNumChildren(NodePtrType node) {
            return (node->left != NULL) + (node->right != NULL);
        }
        size_t getNumKeys(NodePtrType node) {
            if (!isLeaf(node)) return 0;
            if (node->key == minKey || node->key == maxKey) return 0;
            return 1;
        }
        size_t getSumOfKeys(NodePtrType node) {
            if (getNumKeys(node) == 0) return 0;
            return (size_t) node->key;
        }
        ChildIterator getChildIterator(NodePtrType node) {
            return ChildIterator(node);
        }
        static size_t getSizeInBytes(NodePtrType node) { return sizeof(*node); }
    };
    TreeStats<NodeHandler> * createTreeStats(const K& _minKey, const K& _maxKey) {
        return new TreeStats<NodeHandler>(new NodeHandler(_minKey, _maxKey), ds->get_root()->left, true);
    }
#endif
};

#endif
