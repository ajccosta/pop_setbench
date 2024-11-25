/**
 * DS= lock free linked list , harris list. 
 * LINK= https://www.microsoft.com/en-us/research/wp-content/uploads/2001/10/2001-disc.pdf
 * 
 * ASCYLIB: https://github.com/LPD-EPFL/ASCYLIB-Cpp/blob/master/src/algorithms/linkedlist_harris.h
 * TITLE= A Pragmatic Implementation of Non-Blocking Linked-Lists by Tim Harris
 * Harris list doesnt work with Hyaline crystalline as mentioned in paper. FOr WFE also it doesnt work. Reason traversal of sequence of 
 * marked nodes.
 */

#ifndef HARRISLIST_DAOI_RUSLON_IMPL_H
#define HARRISLIST_DAOI_RUSLON_IMPL_H

#include "record_manager.h"
#include <string>
using namespace std;

template<typename K, typename V>
class node_t {
public:
    K key;
    V val;
    // node_t<K,V> * volatile next;
    std::atomic< node_t<K,V> *> next;
};


static inline bool is_marked_ref(void *i)
{
    return (bool) ((uintptr_t) i & 0x1L);
}

static inline void *get_unmarked_ref(void *w)
{
    return (void *) ((uintptr_t) w & ~0x1L);
}

static inline void *get_marked_ref(void *w)
{
    return (void *) ((uintptr_t) w | 0x1L);
}

#define nodeptr node_t<K,V> *

template <typename K, typename V, class RecManager>
class harrislistDAOIRUSLON {
private:
    RecManager * const recmgr;
    nodeptr head;
    nodeptr tail;

    const K KEY_MIN;
    const K KEY_MAX;
    const V NO_VALUE;
    int flag=0;


    // bool validateLinks(const int tid, nodeptr pred, nodeptr curr);
    nodeptr new_node(const int tid, const K& key, const V& val, nodeptr next);
    long long debugKeySum(nodeptr head);

    V doInsert(const int tid, const K& key, const V& value, bool onlyIfAbsent);
    nodeptr list_search(const int tid, const K& key, const V& value, nodeptr *left_node);
    
    int init[MAX_THREADS_POW2] = {0,};

public:

    harrislistDAOIRUSLON(int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V NO_VALUE, unsigned int id);
    ~harrislistDAOIRUSLON();
    bool contains(const int tid, const K& key);
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        return doInsert(tid, key, val, true);
    }
    V erase(const int tid, const K& key);
    
    void initThread(const int tid);
    void deinitThread(const int tid);
    
    long long debugKeySum();
    void printlist();
    long long getKeyChecksum();
    bool validate(const long long keysum, const bool checkkeysum) {
        return true;
    }

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
harrislistDAOIRUSLON<K,V,RecManager>::harrislistDAOIRUSLON(const int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V _NO_VALUE, unsigned int id)
        : recmgr(new RecManager(numProcesses, /*SIGRTMIN+1*/SIGQUIT)), KEY_MIN(_KEY_MIN), KEY_MAX(_KEY_MAX), NO_VALUE(_NO_VALUE)
{
    const int tid = 0;
    initThread(tid);
    tail = new_node(tid, KEY_MAX, 0, NULL);
    head = new_node(tid, KEY_MIN, 0, tail);

    // printlist();

}

template <typename K, typename V, class RecManager>
harrislistDAOIRUSLON<K,V,RecManager>::~harrislistDAOIRUSLON() {
    const int dummyTid = 0;
    nodeptr curr = head;
    while (curr != NULL) {
        nodeptr next = (nodeptr)get_unmarked_ref(curr->next);//curr->next;
        recmgr->deallocate(dummyTid, curr);
        curr = next;
    }
    // COUTATOMICTID("curr->key="<<curr->key<<std::endl);

    recmgr->deallocate(dummyTid, curr);
   
    recmgr->printStatus();
    delete recmgr;
}

template <typename K, typename V, class RecManager>
void harrislistDAOIRUSLON<K,V,RecManager>::initThread(const int tid) {
//    if (init[tid]) return; else init[tid] = !init[tid];

    recmgr->initThread(tid);
}

template <typename K, typename V, class RecManager>
void harrislistDAOIRUSLON<K,V,RecManager>::deinitThread(const int tid) {
//    if (!init[tid]) return; else init[tid] = !init[tid];

    recmgr->deinitThread(tid);
}

template <typename K, typename V, class RecManager>
nodeptr harrislistDAOIRUSLON<K,V,RecManager>::new_node(const int tid, const K& key, const V& val, nodeptr next) {

    nodeptr nnode = recmgr->template allocate<node_t<K,V> >(tid);//dummy call to record stats
    nnode = recmgr->template instrumentedAlloc<node_t<K,V> >(tid);

    if (nnode == NULL) {
        cout<<"out of memory"<<endl;
        exit(1);
    }
    nnode->key = key;
    nnode->val = val;
    // nnode->next = next;
    nnode->next.store(next, std::memory_order_release);
    return nnode;
}

template <typename K, typename V, class RecManager>
void harrislistDAOIRUSLON<K,V,RecManager>:: printlist()
{
    nodeptr curr = (nodeptr)get_unmarked_ref(head); //head->next;
    while (curr != NULL)
    {
        COUTATOMIC(curr<<" -->");
        curr = (nodeptr)get_unmarked_ref(curr->next) ; 
    }

}

// Note, if an address in next field of node A is marked then Node A is marked for deletion.
// Always confused me just making a self note.
template <typename K, typename V, class RecManager>
nodeptr harrislistDAOIRUSLON<K,V,RecManager>::list_search(const int tid, const K& key, const V& value, nodeptr *left_node) {

    nodeptr left_node_next = head;
    nodeptr right_node = NULL;
    uint64_t idx = 0;
    size_t cntseqnodes = 0;
#ifdef DAOI_RUSLONRDPTR_RECLAIMERS
nodeptr prevBlock = nullptr;
#endif

    do {
        nodeptr t = head;

#ifdef DAOI_RUSLONRDPTR_RECLAIMERS
        nodeptr t_next = recmgr->readByPtrToTypeAndPtr(tid, (idx++%2), head->next, head); //head->next;
#else
        nodeptr t_next = recmgr->read(tid, (idx++%2), head->next); //head->next;
#endif

        do {
            if (!is_marked_ref(t_next))
            {
                (*left_node) = t;
                left_node_next = t_next;
                cntseqnodes=0;
            }
            t = (nodeptr)get_unmarked_ref(t_next);
            if (t == tail || !t->next)
                break;

            // t_next = recmgr->read(tid, (idx++%2), t->next); //t->next;
#ifdef DAOI_RUSLONRDPTR_RECLAIMERS
            t_next = recmgr->readByPtrToTypeAndPtr(tid, (idx++%2), t->next, t); //t->next;
#else
            t_next = recmgr->read(tid, (idx++%2), t->next); //t->next;
#endif            
        } while (is_marked_ref(t_next) || (t->key < key));

        right_node = t; //unmarked node greater than key.

        if (left_node_next == right_node) { 
            if (( (right_node->next.load(std::memory_order_seq_cst)) ) && (is_marked_ref(right_node->next.load(std::memory_order_seq_cst))) ){ 
                continue;
            }
            else
            {
                return right_node;
            }
        }

        if (((*left_node)->next).compare_exchange_strong(left_node_next, right_node, std::memory_order_seq_cst)) {
            nodeptr cur = left_node_next;
            do{
                nodeptr node_to_free = cur;
                cur = (nodeptr)get_unmarked_ref(cur->next.load());
                // assert(node_to_free);
                // recmgr->retire(tid, node_to_free);
            }while(cur != right_node);

            if (
                !( (right_node->next.load(std::memory_order_seq_cst)) 
                && 
                (is_marked_ref(right_node->next.load(std::memory_order_seq_cst))) )
                )
            {
                return right_node;
            }
        }
    }while(1);
}

template <typename K, typename V, class RecManager>
bool harrislistDAOIRUSLON<K,V,RecManager>::contains(const int tid, const K& key) {

    auto guard = recmgr->getGuard(tid, true);

    nodeptr right = NULL;
    nodeptr left = head;
    right = list_search(tid, key, NO_VALUE, &left);
    
    if (right->next == NULL || right->key != key)
    {
        return false; 
    }
    else
    {
        return true;
    }
}

template <typename K, typename V, class RecManager>
V harrislistDAOIRUSLON<K,V,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {

    auto guard = recmgr->getGuard(tid, true);

    nodeptr right = NULL;
    nodeptr left = head;
    nodeptr new_elem = NULL;
   
    while (1) {
        right = list_search(tid, key, val, &left);
        if (right->key == key)
        {
            return right->val;//&tail; //right->val; //failed op ins
        }
        
        if (new_elem == NULL)
        {
            new_elem = new_node(tid, key, val, NULL);
        }

        new_elem->next.store(right, std::memory_order_release);

        // if (CASB(&(left->next), right, new_elem)) {
        if (left->next.compare_exchange_strong(right,new_elem,std::memory_order_acq_rel)) {

            return NO_VALUE; //succes op ins
        }
    } //while
}

/*
 * Logically remove an element by setting a mark bit to 1 
 * before removing it physically.
 */
template <typename K, typename V, class RecManager>
V harrislistDAOIRUSLON<K,V,RecManager>::erase(const int tid, const K& key) {
    auto guard = recmgr->getGuard(tid, true);

    nodeptr right = NULL;
    nodeptr left = head;
    nodeptr right_succ = NULL;
    V ret = 0;
    while (1) 
    {
        right = list_search(tid, key, NO_VALUE, &left);
        // if we reached tail or key was not found return del op fail
        if (right->key != key){
            return NO_VALUE; //del op fail
        }
        right_succ = right->next.load();

        // if the next field pointer in the right node is not marked. Meaning right node is not marked for deletion.
        // try to mark it else retry the delet op some conc interefence occurred which modified right node
        if (!is_marked_ref(right_succ)) {
            // if (CASB(&(right->next), right_succ, get_marked_ref(right_succ))) { //right node is marked but not unlinked yet.
            if (right->next.compare_exchange_strong(right_succ, (nodeptr)get_marked_ref(right_succ), std::memory_order_acq_rel)) { //right node is marked but not unlinked yet.
                ret = right->val;
                break;         
            }
        }
    }

    // when can marking sucxced but unlining fail? when left node not pointing to right.
    // if left nopde still points to right then unlink. else I will search again to unlink this node.
    // if (!CASB(&left->next, right, right_succ)) {
    if (left->next.compare_exchange_strong(right, right_succ, std::memory_order_acq_rel)) {
        // safe to retire here.
        // as this thread that marked is unlinking it.
        // assert(!is_marked_ref(left->next));
        assert(!is_marked_ref(right));
        recmgr->retire(tid, (nodeptr)get_unmarked_ref(right));
    }
    else
    {
        // case if unlinking failed.
        // Who should retire this node?
        right = list_search(tid, key, NO_VALUE, &left);
    }
    //delete op success retire it in else case.
    return ret;//right->val; //del op success
}

template <typename K, typename V, class RecManager>
long long harrislistDAOIRUSLON<K,V,RecManager>::debugKeySum(nodeptr head) {
    long long result = 0;
    int marked_count = 0;
    cout << "start ********************AJ checksum"<<result<<endl;
    nodeptr curr = (nodeptr)get_unmarked_ref(head->next);
    while (curr->key < KEY_MAX) 
    {
       if (is_marked_ref(curr->next)) 
       {
            marked_count++;
       }
       else
       {
           result += curr->key;
       }

        curr = (nodeptr)get_unmarked_ref(curr->next) ;//curr->next;
    }
    cout << "end ********************AJ checksum="<<result<<": marked_count ="<<marked_count<<endl;
    return result;
}

template <typename K, typename V, class RecManager>
long long harrislistDAOIRUSLON<K,V,RecManager>::getDSSize() 
{
    long long result = 0;
    nodeptr curr = (nodeptr)get_unmarked_ref(head->next); //head->next;
    while (curr->key < KEY_MAX) {
        // result += 1;
       if (!is_marked_ref(curr->next)) 
       {
            result += 1;
       }

        curr = (nodeptr)get_unmarked_ref(curr->next); //curr->next;
    }
    return result;
}

template <typename K, typename V, class RecManager>
long long harrislistDAOIRUSLON<K,V,RecManager>::getKeyChecksum() 
{
    return debugKeySum(head);
}

template <typename K, typename V, class RecManager>
long long harrislistDAOIRUSLON<K,V,RecManager>::debugKeySum() {
//    COUTATOMIC("debugKeySum="<<debugKeySum(head)<<std::endl);
    return debugKeySum(head);
}

#endif	/* HARRISLIST_DAOI_RUSLON_IMPL_H */
