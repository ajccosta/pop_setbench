/**
 * C++ record manager implementation (PODC 2015) by Trevor Brown.
 * 
 * Copyright (C) 2015 Trevor Brown
 *
 */

#ifndef RECLAIM_NOOP_H
#define	RECLAIM_NOOP_H

#include <cassert>
#include <iostream>
#include "pool_interface.h"
#include "reclaimer_interface.h"
#include "blockbag.h"


template <typename T = void, class Pool = pool_interface<T> >
class reclaimer_none : public reclaimer_interface<T, Pool> {
private:
#ifdef ALLOCATOR_BOTTLENECK_TEST
        blockbag<T> **retiredBag;
        PAD;
#endif //ALLOCATOR_BOTTLENECK_TEST  
    class ThreadData
    {
    private:
        PAD;
    public:

    // to save global reservations once before emptying retired bag.
    uint64_t bagsizeaccumulated;

    ThreadData()
    {
    }
    private:
        PAD;
    };

    // PAD;
    ThreadData threadData[MAX_THREADS_POW2];
    PAD;          
public:
    PAD; // post padding for superclass layout
    
    template<typename _Tp1>
    struct rebind {
        typedef reclaimer_none<_Tp1, Pool> other;
    };
    template<typename _Tp1, typename _Tp2>
    struct rebind2 {
        typedef reclaimer_none<_Tp1, _Tp2> other;
    };
    
    std::string getDetailsString() { return "no reclaimer"; }
    std::string getSizeString() { return "no reclaimer"; }
    inline static bool shouldHelp() {
        return true;
    }
    
    inline static bool isQuiescent(const int tid) {
        return true;
    }
    inline static bool isProtected(const int tid, T * const obj) {
        return true;
    }
    inline static bool isQProtected(const int tid, T * const obj) {
        return false;
    }
    
    // for hazard pointers (and reference counting)
    inline static bool protect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) {
        return true;
    }
    inline static void unprotect(const int tid, T * const obj) {}
    inline static bool qProtect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) {
        return true;
    }
    inline static void qUnprotectAll(const int tid) {}
    
    // rotate the epoch bags and reclaim any objects retired two epochs ago.
    inline static void rotateEpochBags(const int tid) {
    }
    // invoke this at the beginning of each operation that accesses
    // objects reclaimed by this epoch manager.
    // returns true if the call rotated the epoch bags for thread tid
    // (and reclaimed any objects retired two epochs ago).
    // otherwise, the call returns false.
    template <typename First, typename... Rest>
    inline 
#ifndef ALLOCATOR_BOTTLENECK_TEST 
    static 
#endif
     bool startOp(const int tid, void * const * const reclaimers, const int numReclaimers, const bool readOnly = false) {
#ifdef ALLOCATOR_BOTTLENECK_TEST
    int num_nodes = (retiredBag[tid]->isEmpty()? 0: (retiredBag[tid]->getSizeInBlocks()-1)*BLOCK_SIZE + retiredBag[tid]->getHeadSize());
    if (num_nodes >= (MAX_RINGBAG_CAPACITY_POW2))
    {
        TRACE COUTATOMICTID("Before freeing retiredBag size in nodes="<<num_nodes<<" thresh="<<MAX_RINGBAG_CAPACITY_POW2<<std::endl);

        T* ptr;
        int i = 0;
        while(!retiredBag[tid]->isEmpty()){
            ptr = retiredBag[tid]->remove();
            this->pool->add(tid, ptr);
            i++;
        }
        assert(i == num_nodes);
        TRACE COUTATOMICTID("After freeing retiredBag size in nodes="<<num_nodes<<std::endl);
    }
    return true;
#endif //#ifdef ALLOCATOR_BOTTLENECK_TEST        
        
        return false;
    }
    inline static void endOp(const int tid) {
    }
    
    // for all schemes except reference counting
    inline
// #ifndef ALLOCATOR_BOTTLENECK_TEST 
//     static
// #endif
    void retire(const int tid, T* p) {
        threadData[tid].bagsizeaccumulated += 1;
// #ifdef ALLOCATOR_BOTTLENECK_TEST
        // if (p == NULL) assert ("null record"&& 0);
        // if (p == NULL) return;
        // retiredBag[tid]->add(p);
// #endif // ALLOCATOR_BOTTLENECK_TEST
        if (threadData[tid].bagsizeaccumulated % 1000 == 0)
        {
        #ifdef USE_GSTATS
            GSTATS_APPEND(tid, reclamation_event_size, threadData[tid].bagsizeaccumulated);
        #endif
        }
    }

    void debugPrintStatus(const int tid) {
    }

//    set_of_bags<T> getBlockbags() {
//        set_of_bags<T> empty = {.bags = NULL, .numBags = 0};
//        return empty;
//    }
//    
//    void getOldestTwoBlockbags(const int tid, blockbag<T> ** oldest, blockbag<T> ** secondOldest) {
//        *oldest = *secondOldest = NULL;
//    }
//    
//    int getOldestBlockbagIndexOffset(const int tid) {
//        return -1;
//    }
    
    void getSafeBlockbags(const int tid, blockbag<T> ** bags) {
        bags[0] = NULL;
    }
    
    void initThread(const int tid) {
        threadData[tid].bagsizeaccumulated = 0;
    }
    void deinitThread(const int tid) {
        GSTATS_APPEND(tid, reclamation_event_size, threadData[tid].bagsizeaccumulated);
    }

    reclaimer_none(const int numProcesses, Pool *_pool, debugInfo * const _debug, RecoveryMgr<void *> * const _recoveryMgr = NULL)
            : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr) {
        VERBOSE DEBUG std::cout<<"constructor reclaimer_none"<<std::endl;
#ifdef ALLOCATOR_BOTTLENECK_TEST        
        retiredBag = new blockbag<T> * [numProcesses*PREFETCH_SIZE_WORDS];
        for (int tid  = 0; tid < numProcesses; ++tid){
            retiredBag[tid] = new blockbag<T>(tid, this->pool->blockpools[tid]);
        }
#endif //#ifdef ALLOCATOR_BOTTLENECK_TEST        
    }
    ~reclaimer_none() {
        VERBOSE DEBUG std::cout<<"destructor reclaimer_none"<<std::endl;

        // #ifdef USE_GSTATS
        // for (int tid  = 0; tid < this->NUM_PROCESSES; ++tid){        
            // GSTATS_APPEND(tid, reclamation_event_size, ((retiredBag[tid]->getSizeInBlocks() - 1) * BLOCK_SIZE + retiredBag[tid]->getHeadSize()));
            // std::cout <<"max_reclamation_event_size_total="<<((threadData[tid].bagsizeaccumulated)<<std::endl;
            // std::cout <<"max_reclamation_event_size_total="<<((retiredBag[4]->getSizeInBlocks() - 1) * BLOCK_SIZE + retiredBag[4]->getHeadSize());
        // }
        // #endif   

#ifdef ALLOCATOR_BOTTLENECK_TEST        
        for (int tid  = 0; tid < this->NUM_PROCESSES; ++tid){
            this->pool->addMoveAll(tid, retiredBag[tid]);
            delete retiredBag[tid];
        }
        delete[] retiredBag;
#endif //ALLOCATOR_BOTTLENECK_TEST
    }

}; // end class

#endif

