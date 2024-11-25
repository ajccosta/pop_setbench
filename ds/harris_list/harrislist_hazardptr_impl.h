/**
 * DS= lock free linked list , harris list. 
 * LINK= https://www.microsoft.com/en-us/research/wp-content/uploads/2001/10/2001-disc.pdf
 * TITLE= A Pragmatic Implementation of Non-Blocking Linked-Lists by Tim Harris
 * 
 * Incomplete: If apply HP to harris list then it becomes HMlist.
 * Also not clear how to apply Trev HP implentation to it as after gettiong a left and right node in search we also delete all
 * marked nodes between them on by one. Traversing such nodes will continuously cause HP to restart forever.
 * 
 */

#ifndef harrislistHAZARDPTR_HAZARDPTR_IMPL_H
#define harrislistHAZARDPTR_HAZARDPTR_IMPL_H

#include "record_manager.h"
#include "locks_impl.h"
#include <string>
using namespace std;

template<typename K, typename V>
class node_t {
public:
    K key;
    V val;
    node_t<K,V> * volatile next;
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
class harrislistHAZARDPTR {
private:
    RecManager * const recmgr;
    nodeptr head;
    nodeptr tail;

    const K KEY_MIN;
    const K KEY_MAX;
    const V NO_VALUE;


    // bool validateLinks(const int tid, nodeptr pred, nodeptr curr);
    nodeptr new_node(const int tid, const K& key, const V& val, nodeptr next);
    long long debugKeySum(nodeptr head);

    V doInsert(const int tid, const K& key, const V& value, bool onlyIfAbsent);
    nodeptr list_search(const int tid, const K& key, const V& value, nodeptr *left_node);
    
    int init[MAX_THREADS_POW2] = {0,};

public:

    harrislistHAZARDPTR(int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V NO_VALUE, unsigned int id);
    ~harrislistHAZARDPTR();
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
            atomic_uintptr_t *_ptrToObj)
            : obj(_obj),
              ptrToObj(_ptrToObj)
              {}
    BST_retired_info() {}
};

#define IF_FAIL_TO_PROTECT_NODE(info, tid, _obj, arg2) \
    info.obj = _obj; \
    info.ptrToObj = (atomic_uintptr_t *) arg2; \
    if (_obj != head && !recmgr->protect(tid, _obj, callbackCheckNotRetired, (void*) &info))

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
        
        if (! is_marked_ref( (void*) info->ptrToObj->load(memory_order_relaxed) ) ) {
            return true;
        }
    }
    return false;
}

template <typename K, typename V, class RecManager>
harrislistHAZARDPTR<K,V,RecManager>::harrislistHAZARDPTR(const int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V _NO_VALUE, unsigned int id)
        : recmgr(new RecManager(numProcesses, /*SIGRTMIN+1*/SIGQUIT)), KEY_MIN(_KEY_MIN), KEY_MAX(_KEY_MAX), NO_VALUE(_NO_VALUE)
{
    const int tid = 0;
    initThread(tid);
    tail = new_node(tid, KEY_MAX, 0, NULL);
    head = new_node(tid, KEY_MIN, 0, tail);
}

template <typename K, typename V, class RecManager>
harrislistHAZARDPTR<K,V,RecManager>::~harrislistHAZARDPTR() {
    const int dummyTid = 0;
    nodeptr curr = head;
    while (curr->key < KEY_MAX) {
        // COUTATOMICTID("curr->key="<<curr->key<<std::endl);
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
void harrislistHAZARDPTR<K,V,RecManager>::initThread(const int tid) {
//    if (init[tid]) return; else init[tid] = !init[tid];

    recmgr->initThread(tid);
}

template <typename K, typename V, class RecManager>
void harrislistHAZARDPTR<K,V,RecManager>::deinitThread(const int tid) {
//    if (!init[tid]) return; else init[tid] = !init[tid];

    recmgr->deinitThread(tid);
}

template <typename K, typename V, class RecManager>
nodeptr harrislistHAZARDPTR<K,V,RecManager>::new_node(const int tid, const K& key, const V& val, nodeptr next) {
    nodeptr nnode = recmgr->template allocate<node_t<K,V> >(tid);
    if (nnode == NULL) {
        cout<<"out of memory"<<endl;
        exit(1);
    }
    nnode->key = key;
    nnode->val = val;
    nnode->next = next;

    return nnode;
}


// Note, if an address in next field of node A is marked then Node A is marked for deletion.
// Always confused me just making a self note.
template <typename K, typename V, class RecManager>
nodeptr harrislistHAZARDPTR<K,V,RecManager>::list_search(const int tid, const K& key, const V& value, nodeptr *left_node) {
retry:
    nodeptr left_node_next = NULL;
    nodeptr right_node = NULL;
    BST_retired_info info;

    while (1) 
    {
        nodeptr t = head;
        // IF_FAIL_TO_PROTECT_NODE(info, tid, t, &head) {
        //     continue; /* retry */ 
        // }
        COUTATOMICTID("protected head"<<std::endl);
        
        nodeptr t_next = head->next;
        IF_FAIL_TO_PROTECT_NODE(info, tid, t_next, &head->next) {
            continue; /* retry */ 
        }
        assert(!is_marked_ref(head->next));
        COUTATOMICTID("protected head->next"<<std::endl);

        // find left and right node
        // enroute will skip sequence of marked node unless first unmarked node is found.
        // leftnode will have nodeptr for first unmarked node and right_node will have next immediate unmarked node.
        while (is_marked_ref(t_next) || (t->key < key))
        {
            // update leftnode and left_node_next if t_next is not marked
            // always keeps track of leftmost unmarked node found so far
            if (!is_marked_ref(t_next))
            {
                (*left_node) = t;
                left_node_next = t_next;
            }

            recmgr->unprotect(tid, t);

            t = (nodeptr)get_unmarked_ref(t_next);
            
            if (t == tail || !t->next /* this change to enable reclamation: t->next was null which lead to crash in read() while load() the fix copied from ASCYLIB*/ )
                break;
            t_next = t->next;
            IF_FAIL_TO_PROTECT_NODE(info, tid, t_next, &t->next) {
                continue; /* retry */ 
            }            
        }

        right_node = t;

        //validate left still points to right and right is not marked. then return left, right pair
        // case where so far marked nodes were not found in traversal
        if (left_node_next == right_node) { 
            // case where someone marked the right node since I last saw it unmarked. I will retry.
            if ((right_node != tail) && (is_marked_ref(right_node->next)) ){
                goto retry;
            }
            else
            {
                // no one marked the right node since I last saw it in traversal loop. So I found correct left and right node which I can return.
                // left and right nodes are prev and curr where updates could take effect. 
                return right_node;
            }
        }

        // case: if a sequence of marked node were found then unlink them leftnode -> -- marked nodes --- -> right node
        // I will unlink the sequence of marked nodes and then recheck again that of my right node is still not concurrently marked.
        if (CASB(&((*left_node)->next), left_node_next, right_node)) {
            //retire sequence of marked nodes begin
            // this change to enable reclamation
            nodeptr cur = left_node_next;
            do{
                nodeptr node_to_free = cur;
                cur = (nodeptr)get_unmarked_ref(cur->next);
                recmgr->retire(tid, node_to_free);
            }while(cur != right_node);
            //retire sequence of marked nodes end

            // case where someone marked the right node since I last saw it unmarked. I will retry.
            if ((right_node != tail) && (is_marked_ref(right_node->next)) ){
                //Only one thread could be executing this by virtue of property of CAS
                // is it safe to retire here??
                // I have to add a sequence of marked and now unlinked nodes to the retire bag.
                // MEMLEAK: If I donot call retire here, then sequence of memory node could be leaked
                goto retry;
            }
            else
            {
                return right_node;
            }
        }
    } //while
}


template <typename K, typename V, class RecManager>
bool harrislistHAZARDPTR<K,V,RecManager>::contains(const int tid, const K& key) {

    auto guard = recmgr->getGuard(tid, true);

    nodeptr right = NULL;
    nodeptr left = NULL;
    right = list_search(tid, key, NO_VALUE, &left);
    
    if (right == tail || right->key != key)
    {
        return false; 
    }
    else
    {
        return true;
    }
}

template <typename K, typename V, class RecManager>
V harrislistHAZARDPTR<K,V,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {

    auto guard = recmgr->getGuard(tid, true);

    nodeptr right = NULL;
    nodeptr left = NULL;
    nodeptr new_elem = new_node(tid, key, val, NULL);

    while (1) {
        right = list_search(tid, key, val, &left);
        if (right != tail && right->key == key)
        {
            recmgr->deallocate(tid, new_elem);
            return &tail; //right->val; //failed op ins
        }
        
        new_elem->next = right;
        if (CASB(&(left->next), right, new_elem)) {
            return NO_VALUE; //succes op ins
        }
    } //while
}

/*
 * Logically remove an element by setting a mark bit to 1 
 * before removing it physically.
 */
template <typename K, typename V, class RecManager>
V harrislistHAZARDPTR<K,V,RecManager>::erase(const int tid, const K& key) {
    recmgr->startOp(tid);

    nodeptr right = NULL;
    nodeptr left = NULL;
    nodeptr right_succ = NULL;

    while (1) 
    {
        right = list_search(tid, key, NO_VALUE, &left);
        // if we reached tail or key was not found return del op fail
        if (right == tail || right->key != key){
            return NO_VALUE; //del op fail
        }
        right_succ = right->next;

        // if the next field pointer in the right node is not marked. Meaning right node is not marked for deletion.
        // try to mark it else retry the delet op some conc interefence occurred which modified right node
        if (!is_marked_ref(right_succ)) {
            if (CASB(&(right->next), right_succ, get_marked_ref(right_succ))) { //right node is marked but not unlinked yet.
                // recmgr->retire(tid, right);
                break;         
            }
        }        
    }

    // when can marking sucxced but unlining fail? when left node not pointing to right.
    // if left nopde still points to right then unlink. else I will search again to unlink this node.
    if (!CASB(&left->next, right, right_succ)) {
        // case if unlinking failed.
        // Who should retire this node?
        right = list_search(tid, right->key, NO_VALUE, &left);
    }
    else
    {
        // safe to retire here.
        // as this thread that marked is unlinking it.
        recmgr->retire(tid, right);
    }
    //delete op success retire it in else case.
    return &tail;//right->val; //del op success
}

template <typename K, typename V, class RecManager>
long long harrislistHAZARDPTR<K,V,RecManager>::debugKeySum(nodeptr head) {
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
long long harrislistHAZARDPTR<K,V,RecManager>::getDSSize() 
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
long long harrislistHAZARDPTR<K,V,RecManager>::getKeyChecksum() 
{
    return debugKeySum(head);
}

template <typename K, typename V, class RecManager>
long long harrislistHAZARDPTR<K,V,RecManager>::debugKeySum() {
//    COUTATOMIC("debugKeySum="<<debugKeySum(head)<<std::endl);
    return debugKeySum(head);
}

#endif	/* harrislistHAZARDPTR_HAZARDPTR_IMPL_H */
