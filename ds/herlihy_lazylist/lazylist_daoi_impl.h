/**
 *   This algorithm is based on: 
 *   A Lazy Concurrent List-Based Set Algorithm,
 *   S. Heller, M. Herlihy, V. Luchangco, M. Moir, W.N. Scherer III, N. Shavit
 *   OPODIS 2005
 *   
 *   The implementation is based on implementations by:
 *   Vincent Gramoli https://sites.google.com/site/synchrobench/
 *   Vasileios Trigonakis http://lpd.epfl.ch/site/ascylib - http://lpd.epfl.ch/site/optik
 * 
 * the lazylistDOOI version implements data and operation only instrumentation based reclaimers like: 2geibr which require
 * birth and retire era and also C++ atomic API like in IBR git code.
 */

#ifndef LAZYLIST_DAOI_IMPL_H
#define LAZYLIST_DAOI_IMPL_H

#include "record_manager.h"
#include <string>
using namespace std;

template<typename K, typename V>
class node_t {
public:
    volatile K key;
    volatile V val;
    std::atomic< node_t<K,V> *> next;
    volatile int lock;
    volatile long long marked; // is stored as a long long simply so it is large enough to be used with the lock-free RQProvider (which requires all fields that are modified at linearization points of operations to be at least as large as a machine word)
#ifdef USE_PADDING
    char pad[PAD_SIZE];
#endif
    uint64_t birth_epoch;
};

#define nodeptr node_t<K,V> *

template <typename K, typename V, class RecManager>
class lazylistDAOI {
private:
    RecManager * const recmgr;
    nodeptr head;

    const K KEY_MIN;
    const K KEY_MAX;
    const V NO_VALUE;

    bool validateLinks(const int tid, nodeptr pred, nodeptr curr);
    nodeptr new_node(const int tid, const K& key, const V& val, nodeptr next);
    long long debugKeySum(nodeptr head);
    V doInsert(const int tid, const K& key, const V& value, bool onlyIfAbsent);
    
    int init[MAX_THREADS_POW2] = {0,};

public:

    lazylistDAOI(int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V NO_VALUE, unsigned int id);
    ~lazylistDAOI();

    bool contains(const int tid, const K& key);
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        return doInsert(tid, key, val, true);
    }
    V erase(const int tid, const K& key);
    
    void initThread(const int tid);
    void deinitThread(const int tid);
    
    long long debugKeySum();
    long long getKeyChecksum();
    long long getDSSize();
    
    long long getSizeInNodes() {
        long long size = 0;
        for (nodeptr curr = head->next; curr->key != KEY_MAX; curr = curr->next) {
            ++size;
        }
        return size;
    }
    long long getSize() {
        long long size = 0;
        for (nodeptr curr = head->next; curr->key != KEY_MAX; curr = curr->next) {
            size += (!curr->marked);
        }
        return size;
    }
    string getSizeString() {
        stringstream ss;
        ss<<getSizeInNodes()<<" nodes in data structure";
        return ss.str();
    }
    
    RecManager * const debugGetRecMgr() {
        return recmgr;
    }
   
    node_t<K,V> * debug_getEntryPoint() { return head; }
};

template <typename K, typename V, class RecManager>
lazylistDAOI<K,V,RecManager>::lazylistDAOI(const int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V _NO_VALUE, unsigned int id)
        : recmgr(new RecManager(numProcesses, /*SIGRTMIN+1*/SIGQUIT)), KEY_MIN(_KEY_MIN), KEY_MAX(_KEY_MAX), NO_VALUE(_NO_VALUE)
{
    const int tid = 0;
    initThread(tid);
    nodeptr max = new_node(tid, KEY_MAX, 0, NULL);
    head = new_node(tid, KEY_MIN, 0, max);
}

template <typename K, typename V, class RecManager>
lazylistDAOI<K,V,RecManager>::~lazylistDAOI() {
    const int dummyTid = 0;
    nodeptr curr = head;
    while (curr->key < KEY_MAX) {
        nodeptr next = curr->next;
        recmgr->deallocate(dummyTid, curr);
        curr = next;
    }
    recmgr->deallocate(dummyTid, curr);
    
    recmgr->printStatus();
    
    delete recmgr;
}

template <typename K, typename V, class RecManager>
void lazylistDAOI<K,V,RecManager>::initThread(const int tid) {
    recmgr->initThread(tid);
}

template <typename K, typename V, class RecManager>
void lazylistDAOI<K,V,RecManager>::deinitThread(const int tid) {
    recmgr->deinitThread(tid);
}

template <typename K, typename V, class RecManager>
nodeptr lazylistDAOI<K,V,RecManager>::new_node(const int tid, const K& key, const V& val, nodeptr next) {
    nodeptr nnode = recmgr->template allocate<node_t<K,V> >(tid);
    if (nnode == NULL) {
        cout<<"out of memory"<<endl;
        exit(1);
    }
    nnode->key = key;
    nnode->val = val;
    // nnode->next = next;
    nnode->next.store(next, std::memory_order_release);
    nnode->lock = false;
    nnode->marked = 0;
    nnode->birth_epoch = recmgr->getEpoch();

    return nnode;
}

template <typename K, typename V, class RecManager>
inline bool lazylistDAOI<K,V,RecManager>::validateLinks(const int tid, nodeptr pred, nodeptr curr) {
    return (!pred->marked && !curr->marked && pred->next.load(std::memory_order_acquire) == curr);
}

template <typename K, typename V, class RecManager>
bool lazylistDAOI<K,V,RecManager>::contains(const int tid, const K& key) {
    auto guard = recmgr->getGuard(tid, true);
    nodeptr curr = head;
    while (curr->key < key) {
        curr = recmgr->read(tid, 0, curr->next); //curr->next.load(std::memory_order_acquire);
    }

    V res = NO_VALUE; 
    if ((curr->key == key) && !curr->marked) {
        res = curr->val;
    }
    return (res != NO_VALUE);
}

template <typename K, typename V, class RecManager>
V lazylistDAOI<K,V,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
    nodeptr curr;
    nodeptr pred;
    nodeptr newnode;
    V result;
    uint64_t idx = 0;
    while (true) 
    {
        auto guard = recmgr->getGuard(tid);
        pred = head;
        curr = recmgr->read(tid, (idx++%2), pred->next);
        while (curr->key < key) {
            pred = curr;
            curr = recmgr->read(tid, (idx++%2), curr->next);
        }

        acquireLock(&(pred->lock));
        if (validateLinks(tid, pred, curr)) {
            if (curr->key == key) { //key is in list
                V result = curr->val;
                releaseLock(&(pred->lock));
                return result; //failed
            }
            // success: key not in list insert
            assert(curr->key != key);
            result = NO_VALUE;
            newnode = new_node(tid, key, val, curr);
#ifdef DAOI_IBR_RECLAIMERS //2geibr and HE that need alloc counter updation otherwise is similar to debra/OOI or qsbr OOI_IBR 
            recmgr->updateAllocCounterAndEpoch(tid);
#endif            
            // pred->next = newnode;
            pred->next.store(newnode, std::memory_order_release);

            releaseLock(&(pred->lock));
            return result;
        }
        else
        {

            if(pred->marked)
            {
                TRACE COUTATOMICTID("insert:: validation failed:pred->marked"<<pred->marked<<endl);
            }
            else if(curr->marked){
                TRACE COUTATOMICTID("insert:: validation failed:curr->marked"<<curr->marked<<endl);
            }
            else if(pred->next.load(std::memory_order_acquire) != curr){
                TRACE COUTATOMICTID("insert:: validation failed:pred->next != curr"<<endl);
            }

        }

        releaseLock(&(pred->lock));
    } //while (true)
}

/*
 * Logically remove an element by setting a mark bit to 1 
 * before removing it physically.
 */
template <typename K, typename V, class RecManager>
V lazylistDAOI<K,V,RecManager>::erase(const int tid, const K& key) {
    nodeptr pred;
    nodeptr curr;
    V result;
    uint64_t idx = 0;

    while (true)
    {
        auto guard = recmgr->getGuard(tid);    
        pred = head;
        curr = recmgr->read(tid, (idx++%2), pred->next);
        while (curr->key < key) {
            pred = curr;
            curr = recmgr->read(tid, (idx++%2), curr->next);
        }

        if (curr->key != key) {
            result = NO_VALUE;
            return result;
        }

        acquireLock(&(pred->lock));
        acquireLock(&(curr->lock));
        if (validateLinks(tid, pred, curr)) {
            assert(curr->key == key);
            result = curr->val;
            nodeptr c_nxt = curr->next.load(std::memory_order_acquire);

            curr->marked = 1;                                                   // LINEARIZATION POINT
            pred->next.store(c_nxt, std::memory_order_release);

            recmgr->retire(tid, curr);

            releaseLock(&(curr->lock));
            releaseLock(&(pred->lock));
            return result;
        }
        else
        {

            if(pred->marked)
            {
                TRACE COUTATOMICTID("erase:: validation failed:pred->marked"<<pred->marked<<endl);
            }
            else if(curr->marked){
                TRACE COUTATOMICTID("erase:: validation failed:curr->marked"<<curr->marked<<endl);
            }
            else if(pred->next.load(std::memory_order_acquire) != curr){
                TRACE COUTATOMICTID("erase:: validation failed:pred->next != curr"<<endl);
            }

        }

        releaseLock(&(curr->lock));
        releaseLock(&(pred->lock));
    }//While
}

template <typename K, typename V, class RecManager>
long long lazylistDAOI<K,V,RecManager>::debugKeySum(nodeptr head) {
    long long result = 0;
    nodeptr curr = head->next.load(std::memory_order_relaxed);
    while (curr->key < KEY_MAX) {
        result += curr->key;
        curr = curr->next.load(std::memory_order_relaxed);
    }
    return result;
}

template <typename K, typename V, class RecManager>
long long lazylistDAOI<K,V,RecManager>::getDSSize() 
{
    long long result = 0;
    nodeptr curr = head->next.load(std::memory_order_relaxed);
    while (curr->key < KEY_MAX) {
        result += 1;
        curr = curr->next.load(std::memory_order_relaxed);
    }
    return result;
}

template <typename K, typename V, class RecManager>
long long lazylistDAOI<K,V,RecManager>::getKeyChecksum() 
{
    return debugKeySum(head);
}

template <typename K, typename V, class RecManager>
long long lazylistDAOI<K,V,RecManager>::debugKeySum() {
//    COUTATOMIC("debugKeySum="<<debugKeySum(head)<<std::endl);
    return debugKeySum(head);
}

#endif	/* LAZYLIST_DAOI_IMPL_H */
