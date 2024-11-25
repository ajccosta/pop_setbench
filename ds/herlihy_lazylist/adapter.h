/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   adapter.h
 * Author: a529sing
 *
 * Created on March 4, 2020, 3:19 PM
 */

#ifndef LLIST_ADAPTER_H
#define LLIST_ADAPTER_H

#include <iostream>
#include <csignal>
#include "errors.h"
#include "random_fnv1a.h"
#ifdef USE_TREE_STATS
#   include "tree_stats.h"
#endif

#if defined (OOI_RECLAIMERS) || defined (OOI_POP_RECLAIMERS)
    #include "lazylist_ooi_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T lazylistOOI<K, V, RECORD_MANAGER_T>
#elif NZB_RECLAIMERS
    #include "lazylist_nzb_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T lazylistNZB<K, V, RECORD_MANAGER_T>
#elif HP_RECLAIMERS
    #include "lazylist_hp_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T lazylistHP<K, V, RECORD_MANAGER_T>
#elif defined(IBR_HP_RECLAIMERS) || defined (IBR_HP_POP_RECLAIMERS)
    #include "lazylist_ibr_hp_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T lazylistIBRHP<K, V, RECORD_MANAGER_T>
#elif defined (DAOI_RECLAIMERS) || defined (DAOI_POP_RECLAIMERS)
    #include "lazylist_daoi_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K, V> >
    #define DATA_STRUCTURE_T lazylistDAOI<K, V,  RECORD_MANAGER_T>
// #elif DAOI_POP_RECLAIMERS
//     #include "lazylist_daoi_nzb_impl.h"
//     #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K, V> >
//     #define DATA_STRUCTURE_T lazylistDAOINZB<K, V,  RECORD_MANAGER_T>
#elif DAOI_RUSLON_RECLAIMERS
    #include "lazylist_daoi_ruslon_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K, V> >
    #define DATA_STRUCTURE_T lazylistDAOIRUSLON<K, V,  RECORD_MANAGER_T>
// #elif OOI_POP_RECLAIMERS
//     #include "lazylist_ooi_nzb_impl.h"
//     #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K, V> >
//     #define DATA_STRUCTURE_T lazylistOOINZB<K, V,  RECORD_MANAGER_T>
#elif IBR_RCU_HP_POP_RECLAIMERS
    #include "lazylist_ibr_rcuhppop_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K,V>>
    #define DATA_STRUCTURE_T lazylistIBRRCUHPPOP<K, V, RECORD_MANAGER_T>
#else // legacy way: all reclaimers in same ds impl 
    #include "lazylist_impl.h"
    #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K, V> >
    #define DATA_STRUCTURE_T lazylist<K, V,  RECORD_MANAGER_T>
#endif


// #include "lazylist_impl.h"
// #define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, node_t<K, V> >
// #define DATA_STRUCTURE_T lazylist<K, V,  RECORD_MANAGER_T>


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
    { }
    
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
        return ds->insertIfAbsent(tid, key, val);
    }
    
    V erase(const int tid, const K& key) {
        return ds->erase(tid, key);
    }
    
    V find(const int tid, const K& key) {
        setbench_error("find functionality not implemented for this data structure");
    }
    
    bool contains(const int tid, const K& key) {
        return ds->contains(tid, key);
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        setbench_error("not implemented");
    }
    void printSummary() {
        ds->debugKeySum();
        return;//ds->printDebuggingDetails();
    }
    long long getKeySum() {
        return ds->debugKeySum();
    }

    //used only for lists types not trees
    long long getDSSize() {
        return ds->getDSSize();
    }

    bool validateStructure() {
        return true;//ds->validate();
    }    

    bool isTree()
    {
        return false;
    }
    
    void printObjectSizes() {
        std::cout<<"sizes: node="
                 <<(sizeof(node_t<K, V>))
                 <<std::endl;
    }
    
#ifdef USE_TREE_STATS
// for list types following section is not used.
// keeping this to avoid template instatiation errors.
class NodeHandler {
    public:
        typedef node_t<K, V> * NodePtrType;
        K minKey;
        K maxKey;
        
        NodeHandler(const K& _minKey, const K& _maxKey) {
            minKey = _minKey;
            maxKey = _maxKey;
        }
        
        class ChildIterator {
        private:
            NodePtrType node; // node being iterated over
        public:
            ChildIterator(NodePtrType _node) {
                node = _node;
            }
            
            bool hasNext() {
                // COUTATOMIC("hasnext::"<<node->key<<":"<<(node->next)<<std::endl);
                return node->next != NULL;
            }
            
            NodePtrType next() {
                // COUTATOMIC("next::"<<node->key<<":"<<(node->next->key)<<std::endl);
               return node->next;
            }
        };
        
        bool isLeaf(NodePtrType node) {
            COUTATOMIC("isLeaf::"<<node->key<<":"<<(node->next == NULL)<<std::endl);
            return node->next == NULL ? true : false;
        }
        size_t getNumChildren(NodePtrType node) {
            COUTATOMIC("getNumChildren::"<<node->key<<":"<<(node->next == NULL)<<std::endl);
            return node->next == NULL ? 0 : 1;
        }
        size_t getNumKeys(NodePtrType node) {
            COUTATOMIC("getNumKeys::"<<node->key<<":"<<(node->next == NULL)<<std::endl);
            return node == NULL ? 0 : 1;
        }
        
        size_t getSumOfKeys(NodePtrType node) {
            COUTATOMIC("getSumOfKeys::"<<node->key<<":"<<(node->next == NULL)<<std::endl);
            return (size_t) node->key;
        }
        ChildIterator getChildIterator(NodePtrType node) {
            COUTATOMIC("getChildIterator::"<<node->key<<":"<<(node)<<std::endl);
            return ChildIterator(node);
        }
    };
    TreeStats<NodeHandler> * createTreeStats(const K& _minKey, const K& _maxKey) {
        return new TreeStats<NodeHandler>(new NodeHandler(_minKey, _maxKey), ds->debug_getEntryPoint(), true);
    }
#endif
};

#endif /*DLIST_ADAPTER_H*/

