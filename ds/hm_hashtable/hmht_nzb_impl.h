/**
 * LINK = https://github.com/pramalhe/OneFile/blob/master/datastructures/linkedlists/MagedHarrisLinkedListSetHP.hpp
 * Title = High Performance Dynamic Lock-Free Hash Tablesand List-Based Sets by Maged Michael.
 */

#ifndef HMHT_NZB_IMPL_H
#define HMHT_NZB_IMPL_H

#include "record_manager.h"
#include "locks_impl.h"
#include <string>
using namespace std;


template<typename K, typename V>
class node_t {
public:
    K key;
    V val;
    // node_t<K,V> * volatile next;
    std::atomic<node_t<K,V>*> next;
};

#define nodeptr node_t<K,V> *

template <typename K, typename V, class RecManager>
class hmhtNZB {
private:
    RecManager * const recmgr;
    // std::atomic<nodeptr> head;
    // std::atomic<nodeptr> tail;
PAD;
    paddedAtomic<nodeptr> *buckets; //dynamic array of buckets each slot is a std::atomic<nodeptr> head.
PAD;
    uint num_buckets;
PAD;

    const K KEY_MIN;
    const K KEY_MAX;
    const V NO_VALUE;


    // bool validateLinks(const int tid, nodeptr pred, nodeptr curr);
    nodeptr new_node(const int tid, const K& key, const V& val, nodeptr next);

    V doInsert(const int tid, const K& key, const V& value, bool onlyIfAbsent);
    bool list_search(const int tid, const K& key, std::atomic<nodeptr>* &prev , nodeptr &curr, nodeptr &next);
    
    int init[MAX_THREADS_POW2] = {0,};

public:

    hmhtNZB(int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V NO_VALUE, unsigned int id);
    ~hmhtNZB();
    bool contains(const int tid, const K& key);
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        return doInsert(tid, key, val, true);
    }
    V erase(const int tid, const K& key);
    
    void initThread(const int tid);
    void deinitThread(const int tid);
    
    long long debugKeySum();
    long long getKeyChecksum();
    bool validate(const long long keysum, const bool checkkeysum) {
        return true;
    }

    long long getDSSize();
    

    long long getSizeInNodes() {
        long long size = 0;
        for (uint bid = 0; bid < num_buckets; bid++){  
            // nodeptr curr = buckets[bid].ui.load(); //head.load();
            for (nodeptr curr = (buckets[bid].ui.load())->next; curr->key != KEY_MAX; curr = curr->next) {
                ++size;
            }
        }
        return size;
    }
    long long getSize() {
        long long size = 0;
        for (uint bid = 0; bid < num_buckets; bid++){  
            for (nodeptr curr = (buckets[bid].ui.load())->next; curr->key != KEY_MAX; curr = curr->next) {
                size += (!curr->marked);
            }
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
   
    node_t<K,V> * debug_getEntryPoint() { 
        assert(0 && "hash table has no single entry point");
        return buckets[0].ui.load(); //head; 
    }


    //getMk = isMarked. Checks with mark bit is set
    bool getMk(node_t<K,V> * node) {
    	return ((size_t) node & 0x1);
    }

    //mixPtrMk. get the whole next pointer field with mar bit.
    node_t<K,V> * mixPtrMk(node_t<K,V> * node, bool mk) {
    	return (node_t<K,V>*)((size_t) node | mk);
    }

    //getPtr = getUnmarked. Get the real next pointer field by hiding the mark bit.
    node_t<K,V> * getPtr(node_t<K,V> * node) {
    	return (node_t<K,V>*)((size_t) node & (~0x1));
    }

    //setMk = getMarked. Set the mark field of the pointer.
	inline node_t<K,V>* setMk(node_t<K,V>* node){
		return mixPtrMk(node,true);
	}
 
    void printList() {
        for (uint bid = 0; bid < num_buckets; bid++){  
            nodeptr curr = buckets[bid].ui.load(); //head.load();
            while (curr->key < KEY_MAX) {
                COUTATOMIC("-->"<<curr->key<<"("<<curr<<")"<<"("<<getMk(curr)<<")" );
                curr = (nodeptr)getPtr(curr->next.load()) ;//curr->next;
            }
            COUTATOMIC("-->"<<curr->key<<"("<<curr<<")"<<std::endl<<std::endl);
        }
    }
};

// class BST_retired_info {
// public:
//     void * obj;
//     atomic_uintptr_t * ptrToObj;
//     atomic_bool * nodeContainingPtrToObjIsMarked;
//     BST_retired_info(
//             void *_obj,
//             atomic_uintptr_t *_ptrToObj,
//             atomic_bool * _nodeContainingPtrToObjIsMarked)
//             : obj(_obj),
//               ptrToObj(_ptrToObj),
//               nodeContainingPtrToObjIsMarked(_nodeContainingPtrToObjIsMarked) {}
//     BST_retired_info() {}
// };

// #define IF_FAIL_TO_PROTECT_NODE(info, tid, _obj, arg2, arg3) \
//     info.obj = _obj; \
//     info.ptrToObj = (atomic_uintptr_t *) arg2; \
//     info.nodeContainingPtrToObjIsMarked = (atomic_bool *) arg3; \
//     if (_obj != head && !recmgr->protect(tid, _obj, callbackCheckNotRetired, (void*) &info))

// inline CallbackReturn callbackCheckNotRetired(CallbackArg arg) {
//     BST_retired_info *info = (BST_retired_info*) arg;
//     if ((void*) info->ptrToObj->load(memory_order_relaxed) == info->obj) {
//         // we insert a compiler barrier (not a memory barrier!)
//         // to prevent these if statements from being merged or reordered.
//         // we care because we need to see that ptrToObj == obj
//         // and THEN see that ptrToObject is a field of an object
//         // that is not marked. seeing both of these things,
//         // in this order, implies that obj is in the data structure.
//         SOFTWARE_BARRIER;
//         if (!info->nodeContainingPtrToObjIsMarked->load(memory_order_relaxed)) {
//             return true;
//         }
//     }
//     return false;
// }

template <typename K, typename V, class RecManager>
void hmhtNZB<K,V,RecManager>::initThread(const int tid) {
//    if (init[tid]) return; else init[tid] = !init[tid];

    recmgr->initThread(tid);
}

template <typename K, typename V, class RecManager>
void hmhtNZB<K,V,RecManager>::deinitThread(const int tid) {
//    if (!init[tid]) return; else init[tid] = !init[tid];

    recmgr->deinitThread(tid);
}

template <typename K, typename V, class RecManager>
nodeptr hmhtNZB<K,V,RecManager>::new_node(const int tid, const K& key, const V& val, nodeptr next) {
    nodeptr nnode = recmgr->template allocate<node_t<K,V> >(tid);
    
    if (nnode == NULL) {
        cout<<"out of memory"<<endl;
        exit(1);
    }
    nnode->key = key;
    nnode->val = val;
    nnode->next.store(next);

    return nnode;
}

template <typename K, typename V, class RecManager>
hmhtNZB<K,V,RecManager>::hmhtNZB(const int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V _NO_VALUE, unsigned int id)
        : recmgr(new RecManager(numProcesses, /*SIGRTMIN+1*/SIGQUIT)), KEY_MIN(_KEY_MIN), KEY_MAX(_KEY_MAX), NO_VALUE(_NO_VALUE)
{
    const int tid = 0;
    initThread(tid);

    num_buckets = HASTABLE_SIZE;
    buckets = new paddedAtomic<nodeptr> [num_buckets];
    for (uint bid = 0; bid < num_buckets; bid++){
        nodeptr head;
        nodeptr tail;          
        tail = new_node(tid, KEY_MAX, 0, NULL);
        head = new_node(tid, KEY_MIN, 0, tail);
        buckets[bid].ui.store(head);
    }
}

template <typename K, typename V, class RecManager>
hmhtNZB<K,V,RecManager>::~hmhtNZB() {
    const int dummyTid = 0;
    recmgr->printStatus();

    for (uint bid = 0; bid < num_buckets; bid++){
        nodeptr curr = buckets[bid].ui.load(); //head.load();
        while (curr->key < KEY_MAX) {
            nodeptr next = curr->next.load();
            recmgr->deallocate(dummyTid, curr);
            curr = next;
        }
        recmgr->deallocate(dummyTid, curr);
    }
    delete buckets;
    
    
    delete recmgr;
}

template <typename K, typename V, class RecManager>
bool hmhtNZB<K,V,RecManager>::list_search(const int tid, const K& key, std::atomic<nodeptr>* &prev , nodeptr &curr, nodeptr &nxt) {
    
retry:
    CHECKPOINT_TR(tid, recmgr);
    recmgr->startOp(tid); // make restartable again as thread will restart from head

    while(true)
    {
        bool cmark = false;

        // get atomic pointer to head 
        // prev = &head;
        uint bucketid = key%num_buckets;
        prev = &buckets[bucketid].ui;        
        //load the nodeptr from atomic type
        curr = prev->load();

        while (true) {
            // this cherck seems redundant curr can never be null as we have sentined head and tail
            if (curr == nullptr) 
            {
                assert(0);
                if(recmgr->needsSetJmp()) 
                {
                    recmgr->saveForWritePhase(tid, curr);
                    recmgr->upgradeToWritePhase(tid);
                }
                return false;
            }
            // in base case curr will be nodeptr tail
            nxt = curr->next.load();
            cmark = getMk(nxt);
            nxt = getPtr(nxt);
            // load again the pointer in the next field, somebody marked the pointer/change the actual ptr in next field of curr retry
            if (mixPtrMk(nxt, cmark) != curr->next.load())
            { 
                recmgr->endOp(tid);
                goto retry;                
                // break;
            }
            
            auto ckey = curr->key;
            
            //if somebody changed the next field of the node we reached to curr 
            if (prev->load() != curr) 
            {
                recmgr->endOp(tid);
                goto retry;                
                // break;
            }
            if (!(cmark))
            {
                if (ckey >= key) 
                {
                    // writephase begin
                    if(recmgr->needsSetJmp()) 
                    {
                        recmgr->saveForWritePhase(tid, prev->load());
                        recmgr->saveForWritePhase(tid, curr);
                        recmgr->saveForWritePhase(tid, nxt);
                        recmgr->upgradeToWritePhase(tid);
                    }                 
                    return ckey == key;
                }
                prev = &(curr->next);
            }
            else
            {
                // writephase begin
                if(recmgr->needsSetJmp()) 
                {
                    recmgr->saveForWritePhase(tid, prev->load());
                    recmgr->saveForWritePhase(tid, curr);
                    recmgr->saveForWritePhase(tid, nxt);
                    recmgr->upgradeToWritePhase(tid);
                }                 
                // the curr ptr was marked so unlink it 
                if (prev->compare_exchange_strong(curr, nxt, std::memory_order_acq_rel)) 
                {
                    recmgr->retire(tid, curr);
                }
                else
                {
                    recmgr->endOp(tid);
                    goto retry;                
                    // break;
                }
                recmgr->endOp(tid);
                goto retry;
            }
            curr = nxt;
        } //while(true)
    }//while(true)
}



template <typename K, typename V, class RecManager>
bool hmhtNZB<K,V,RecManager>::contains(const int tid, const K& key) {
    nodeptr curr = nullptr; nodeptr next = nullptr;
    std::atomic<nodeptr> *prev = nullptr;

    bool isContains = list_search(tid, key, prev , curr, next);

    recmgr->endOp(tid);
    return isContains;

}

template <typename K, typename V, class RecManager>
V hmhtNZB<K,V,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
    nodeptr curr = nullptr;
    nodeptr next = nullptr;
    std::atomic<nodeptr> *prev = nullptr;
    nodeptr newNode = new_node(tid, key, val, NULL);
    while (true) {
        if (list_search(tid, key, prev , curr, next)) 
        {
            // There is already a matching key
            //use deallocate of allocator
            recmgr->deallocate(tid, newNode);
            recmgr->endOp(tid);
            return curr->val;
        }

        newNode->next.store(curr, std::memory_order_release);
        if (prev->compare_exchange_strong(curr, newNode, std::memory_order_acq_rel))
        { 
            recmgr->endOp(tid);
            return NO_VALUE;
        }
    }
    assert(0);
}

/*
 * Logically remove an element by setting a mark bit to 1 
 * before removing it physically.
 */
template <typename K, typename V, class RecManager>
V hmhtNZB<K,V,RecManager>::erase(const int tid, const K& key) {
    nodeptr curr = nullptr;
    nodeptr next = nullptr;
    std::atomic<nodeptr> *prev = nullptr;
    
    while (true) {
        /* Try to find the key in the list. */
        if (!list_search(tid, key, prev , curr, next)) {
            recmgr->endOp(tid);
            return NO_VALUE;
        }

        V res = curr->val;
        // attempting marking the curr's next field
        if (!curr->next.compare_exchange_strong(next, setMk(next), std::memory_order_acq_rel)) {
            recmgr->endOp(tid);
            continue; /* Another thread interfered. */
        }

        if (prev->compare_exchange_strong(curr, next, std::memory_order_acq_rel)) { /* Unlink */
            recmgr->retire(tid, curr);
        } 
        else
        {
            recmgr->endOp(tid);

            //failed to unlink the marked node. search again and try removing. 
            list_search(tid, key, prev , curr, next);
        }
        recmgr->endOp(tid);
        return res;
    }
    assert(0);
}

template <typename K, typename V, class RecManager>
long long hmhtNZB<K,V,RecManager>::debugKeySum() {
    long long result = 0;
    int marked_count = 0;

    for (uint bid = 0; bid < num_buckets; bid++){
        nodeptr curr = buckets[bid].ui.load(); //head.load();
        curr = (nodeptr)getPtr(curr->next.load());
        while (curr->key < KEY_MAX) {
            result += curr->key;
        //    COUTATOMIC("-->"<<curr->key<<"("<<curr<<")");
        if (getMk(curr->next.load())) marked_count++;
            curr = (nodeptr)getPtr(curr->next) ;//curr->next;
        }
        assert(marked_count == 0 && "need to not count marked nodes in size calculation");
        // COUTATOMIC("marked_count="<<marked_count<<std::endl);
    }
    // printList();
    return result;
}

template <typename K, typename V, class RecManager>
long long hmhtNZB<K,V,RecManager>::getDSSize() 
{
    long long result = 0;
    for (uint bid = 0; bid < num_buckets; bid++){
        nodeptr curr = buckets[bid].ui.load(); //head.load();
        // nodeptr curr = head.load();
        curr = (nodeptr)getPtr(curr->next.load()); // start from first non sentinel node that is next of head.
        while (curr->key < KEY_MAX) {
            result += 1;
            curr = curr->next.load();
        }
    }
    return result;
}

template <typename K, typename V, class RecManager>
long long hmhtNZB<K,V,RecManager>::getKeyChecksum() 
{
    return debugKeySum();
}

#endif	/* HMHT_NZB_IMPL_H */
