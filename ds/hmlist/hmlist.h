// /* 
//  * File:   harrislist.h
//  * Author: trbot
//  *
//  * Created on February 17, 2016, 7:34 PM
//  */

// #ifndef HMLIST_H
// #define	HMLIST_H
// #include "record_manager.h"
// #include "recovery_manager.h"

// #include <iostream>
// #include <string>
// using namespace std;

// template<typename K, typename V>
// class node_t {
// public:
//     volatile K key;
//     volatile V val;
//     // node_t<K,V> * volatile next;
//     std::atomic<node_t<K,V>*> next;
// };


// static inline bool is_marked_ref(void *i)
// {
//     return (bool) ((uintptr_t) i & 0x1L);
// }

// static inline void *get_unmarked_ref(void *w)
// {
//     return (void *) ((uintptr_t) w & ~0x1L);
// }

// static inline void *get_marked_ref(void *w)
// {
//     return (void *) ((uintptr_t) w | 0x1L);
// }





// #define nodeptr node_t<K,V> *

// template <typename K, typename V, class RecManager>
// class hmlist {
// private:
//     RecManager * const recordmgr;
//     std::atomic<nodeptr> head;
//     std::atomic<nodeptr> tail;

//     const K KEY_MIN;
//     const K KEY_MAX;
//     const V NO_VALUE;


//     // bool validateLinks(const int tid, nodeptr pred, nodeptr curr);
//     nodeptr new_node(const int tid, const K& key, const V& val, nodeptr next);
//     long long debugKeySum(nodeptr head);

//     V doInsert(const int tid, const K& key, const V& value, bool onlyIfAbsent);
//     bool list_search(const int tid, const K& key, std::atomic<nodeptr> **par_prev , nodeptr *par_curr, nodeptr *par_next);
//     // bool find (T* key, std::atomic<Node*> **par_prev, Node **par_curr, Node **par_next, const int tid)
//     //list_search(const int tid, const K& key, const V& value, nodeptr *left_node);
    
//     int init[MAX_THREADS_POW2] = {0,};

// public:

//     hmlist(int numProcesses, const K _KEY_MIN, const K _KEY_MAX, const V NO_VALUE, unsigned int id);
//     ~hmlist();
//     bool contains(const int tid, const K& key);
//     V insertIfAbsent(const int tid, const K& key, const V& val) {
//         return doInsert(tid, key, val, true);
//     }
//     V erase(const int tid, const K& key);
    
//     void initThread(const int tid);
//     void deinitThread(const int tid);
    
//     long long debugKeySum();
//     long long getKeyChecksum();
//     bool validate(const long long keysum, const bool checkkeysum) {
//         return true;
//     }

//     long long getDSSize();
    

//     long long getSizeInNodes() {
//         long long size = 0;
//         for (nodeptr curr = head->next; curr->key != KEY_MAX; curr = curr->next) {
//             ++size;
//         }
//         return size;
//     }
//     long long getSize() {
//         long long size = 0;
//         for (nodeptr curr = head->next; curr->key != KEY_MAX; curr = curr->next) {
//             size += (!curr->marked);
//         }
//         return size;
//     }
//     string getSizeString() {
//         stringstream ss;
//         ss<<getSizeInNodes()<<" nodes in data structure";
//         return ss.str();
//     }
    
//     RecManager * const debugGetRecMgr() {
//         return recordmgr;
//     }
   
//     node_t<K,V> * debug_getEntryPoint() { return head; }


// bool isMarked(node_t<K,V> * node) {
//     	return ((size_t) node & 0x1);
//     }

//     node_t<K,V> * getMarked(node_t<K,V> * node) {
//     	return (node_t<K,V>*)((size_t) node | 0x1);
//     }

//     node_t<K,V> * getUnmarked(node_t<K,V> * node) {
//     	return (node_t<K,V>*)((size_t) node & (~0x1));
//     }


// };

// #include "hmlist_impl.h"
// #endif	/* HMLIST_H */

