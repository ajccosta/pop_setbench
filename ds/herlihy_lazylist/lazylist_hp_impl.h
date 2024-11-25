/**
 *   This algorithm is based on: 
 *   A Lazy Concurrent List-Based Set Algorithm,
 *   S. Heller, M. Herlihy, V. Luchangco, M. Moir, W.N. Scherer III, N. Shavit
 *   OPODIS 2005
 *   
 *   The implementation is based on implementations by:
 *   Vincent Gramoli https://sites.google.com/site/synchrobench/
 *   Vasileios Trigonakis http://lpd.epfl.ch/site/ascylib - http://lpd.epfl.ch/site/optik
 */

#ifndef LAZYLIST_HP_IMPL_H
#define LAZYLIST_HP_IMPL_H

#include "record_manager.h"
#include "locks_impl.h"
#include <string>
using namespace std;

template<typename K, typename V>
class node_t {
public:
    volatile K key;
    volatile V val;
    node_t<K,V> * volatile next;
    volatile int lock;
    volatile long long marked; // is stored as a long long simply so it is large enough to be used with the lock-free RQProvider (which requires all fields that are modified at linearization points of operations to be at least as large as a machine word)
};

#define nodeptr node_t<K,V> *

template <typename K, typename V, class RecManager>
class lazylistHP {
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

    lazylistHP(int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V NO_VALUE, unsigned int id);
    ~lazylistHP();

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





class BST_retired_info {
public:
    void * obj;
    atomic_uintptr_t * ptrToObj;
    atomic_bool * nodeContainingPtrToObjIsMarked;
    BST_retired_info(
            void *_obj,
            atomic_uintptr_t *_ptrToObj,
            atomic_bool * _nodeContainingPtrToObjIsMarked)
            : obj(_obj),
              ptrToObj(_ptrToObj),
              nodeContainingPtrToObjIsMarked(_nodeContainingPtrToObjIsMarked) {}
    BST_retired_info() {}
};


#define UNPROTECT_NODE(tid, _obj) recmgr->unprotect(tid, _obj);

#define IF_FAIL_TO_PROTECT_NODE(info, tid, _obj, arg2, arg3) \
    info.obj = _obj;                                                \
    info.ptrToObj = (std::atomic_uintptr_t *)arg2;                  \
    info.nodeContainingPtrToObjIsMarked = (std::atomic_bool *)arg3; \
    if (_obj != head && !recmgr->protect(tid, _obj, callbackCheckNotRetired, (void *)&info))

inline CallbackReturn callbackCheckNotRetired(CallbackArg arg) {
    BST_retired_info *info = (BST_retired_info*) arg;
    if ((void*) info->ptrToObj->load(memory_order_relaxed) == info->obj) {
        // we insert a compiler barrier (not a memory barrier!)
        // to prevent these if statements from being merged or reordered.
        // we care because we need to see that ptrToObj == obj
        // and THEN see that ptrToObject is a field of an object
        // that is not marked. seeing both of these things,
        // in this order, implies that obj is in the data structure.
        SOFTWARE_BARRIER;
        if (!info->nodeContainingPtrToObjIsMarked->load(memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

template <typename K, typename V, class RecManager>
lazylistHP<K,V,RecManager>::lazylistHP(const int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V _NO_VALUE, unsigned int id)
        : recmgr(new RecManager(numProcesses, /*SIGRTMIN+1*/SIGQUIT)), KEY_MIN(_KEY_MIN), KEY_MAX(_KEY_MAX), NO_VALUE(_NO_VALUE)
{
    const int tid = 0;
    initThread(tid);
    nodeptr max = new_node(tid, KEY_MAX, 0, NULL);
    head = new_node(tid, KEY_MIN, 0, max);
}

template <typename K, typename V, class RecManager>
lazylistHP<K,V,RecManager>::~lazylistHP() {
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
void lazylistHP<K,V,RecManager>::initThread(const int tid) {
    recmgr->initThread(tid);
}

template <typename K, typename V, class RecManager>
void lazylistHP<K,V,RecManager>::deinitThread(const int tid) {
    recmgr->deinitThread(tid);
}

template <typename K, typename V, class RecManager>
nodeptr lazylistHP<K,V,RecManager>::new_node(const int tid, const K& key, const V& val, nodeptr next) {
    nodeptr nnode = recmgr->template allocate<node_t<K,V> >(tid);
    if (nnode == NULL) {
        cout<<"out of memory"<<endl;
        exit(1);
    }
    nnode->key = key;
    nnode->val = val;
    nnode->next = next;
    nnode->lock = false;
    nnode->marked = 0;

    return nnode;
}

template <typename K, typename V, class RecManager>
inline bool lazylistHP<K,V,RecManager>::validateLinks(const int tid, nodeptr pred, nodeptr curr) {
    return (!pred->marked && !curr->marked && pred->next == curr);
}

template <typename K, typename V, class RecManager>
bool lazylistHP<K,V,RecManager>::contains(const int tid, const K& key) {
    BST_retired_info info;
    nodeptr curr;
    nodeptr pred;
    for(;;)
    {
        auto guard = recmgr->getGuard(tid);

        pred = head;
        IF_FAIL_TO_PROTECT_NODE(info, tid, pred, &head, &head->marked) {
            continue; /* retry */ 
        }
        curr = pred->next;
        IF_FAIL_TO_PROTECT_NODE(info, tid, curr, &pred->next, &pred->marked) {
            continue; /* retry */ 
        }
        while (curr->key < key) {
            recmgr->unprotect(tid, pred);
            pred = curr;
            curr = pred->next;
            IF_FAIL_TO_PROTECT_NODE(info, tid, curr, &pred->next, &pred->marked) {
                continue; /* retry */ 
            }
        }

        V res = NO_VALUE; 
        if ((curr->key == key) && !curr->marked) {
            res = curr->val;
        }
        return (res != NO_VALUE);
    }
}

template <typename K, typename V, class RecManager>
V lazylistHP<K,V,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
    BST_retired_info info;
    nodeptr curr;
    nodeptr pred;
    nodeptr newnode;
    V result;
    while (true) 
    {
        auto guard = recmgr->getGuard(tid);

        
        pred = head;
        IF_FAIL_TO_PROTECT_NODE(info, tid, pred, &head, &head->marked) {
            continue; /* retry */ 
        }
        curr = pred->next;
        IF_FAIL_TO_PROTECT_NODE(info, tid, curr, &pred->next, &pred->marked) {
            continue; /* retry */ 
        }
        while (curr->key < key) {
            recmgr->unprotect(tid, pred);
            pred = curr;
            curr = pred->next;
            IF_FAIL_TO_PROTECT_NODE(info, tid, curr, &pred->next, &pred->marked) {
                continue; /* retry */ 
            }
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
            pred->next = newnode;

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
            else if(pred->next != curr){
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
V lazylistHP<K,V,RecManager>::erase(const int tid, const K& key) {
    BST_retired_info info;
    nodeptr pred;
    nodeptr curr;
    V result;
    while (true) {
        auto guard = recmgr->getGuard(tid);

        pred = head;
        IF_FAIL_TO_PROTECT_NODE(info, tid, pred, &head, &head->marked) {
            continue; /* retry */ 
        }
        curr = pred->next;
        IF_FAIL_TO_PROTECT_NODE(info, tid, curr, &pred->next, &pred->marked) {
            continue; /* retry */ 
        }
        
        while (curr->key < key) {
            recmgr->unprotect(tid, pred);
            pred = curr;
            curr = pred->next;
            IF_FAIL_TO_PROTECT_NODE(info, tid, curr, &pred->next, &pred->marked) {
                continue; /* retry */ 
            }
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
            nodeptr c_nxt = curr->next;

            curr->marked = 1;                                                   // LINEARIZATION POINT
            pred->next = c_nxt;

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
            else if(pred->next != curr){
                TRACE COUTATOMICTID("erase:: validation failed:pred->next != curr"<<endl);
            }

        }

        releaseLock(&(curr->lock));
        releaseLock(&(pred->lock));
    }//While
}

template <typename K, typename V, class RecManager>
long long lazylistHP<K,V,RecManager>::debugKeySum(nodeptr head) {
    long long result = 0;
    nodeptr curr = head->next;
    while (curr->key < KEY_MAX) {
        result += curr->key;
//        COUTATOMIC("-->"<<curr->key<<std::endl);
        curr = curr->next;
    }
    return result;
}

template <typename K, typename V, class RecManager>
long long lazylistHP<K,V,RecManager>::getDSSize() 
{
    long long result = 0;
    nodeptr curr = head->next;
    while (curr->key < KEY_MAX) {
        result += 1;
        curr = curr->next;
    }
    return result;
}

template <typename K, typename V, class RecManager>
long long lazylistHP<K,V,RecManager>::getKeyChecksum() 
{
    return debugKeySum(head);
}

template <typename K, typename V, class RecManager>
long long lazylistHP<K,V,RecManager>::debugKeySum() {
//    COUTATOMIC("debugKeySum="<<debugKeySum(head)<<std::endl);
    return debugKeySum(head);
}

#endif	/* LAZYLIST_HP_IMPL_H */
