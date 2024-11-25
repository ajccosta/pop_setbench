/**
 * C++ record manager implementation (PODC 2015) by Trevor Brown.
 * 
 * Copyright (C) 2015 Trevor Brown
 *
 */

#ifndef ALLOC_NEW_H
#define	ALLOC_NEW_H

#include "plaf.h"
#include "pool_interface.h"

#include <cstdlib>
#include <cassert>
#include <iostream>

//__thread long long currentAllocatedBytes = 0;
//__thread long long maxAllocatedBytes = 0;

template<typename T = void>
class allocator_new : public allocator_interface<T> {
    PAD; // post padding for allocator_interface
public:
    template<typename _Tp1>
    struct rebind {
        typedef allocator_new<_Tp1> other;
    };
    
    // reserve space for ONE object of type T
    T* allocate(const int tid) {
        // allocate a new object
        MEMORY_STATS {
            this->debug->addAllocated(tid, 1);
            VERBOSE {
                if ((this->debug->getAllocated(tid) % 2000) == 0) {
                    debugPrintStatus(tid);
                }
            }
//            currentAllocatedBytes += sizeof(T);
//            if (currentAllocatedBytes > maxAllocatedBytes) {
//                maxAllocatedBytes = currentAllocatedBytes;
//            }
        }
// #ifdef DAOI_RUSLON_RECLAIMERS
//     TRACE COUTATOMICTID("allocator::DAOI_RUSLON_RECLAIMERS"<<std::endl);

//     //TODO: This crazy node allocation needs proper deletion as well... do that please.
//     char* block = (char*) malloc(sizeof(uint64_t) + sizeof(T));
//     uint64_t* birth_epoch = (uint64_t*)(block + sizeof(T));
//     //NOTE: I have read globalepoch first for birthera recording and then  allocated the memory. This shallnot make anydiff
//     // because the allocated node hasn't yet been published to other threads. 
//     *birth_epoch = _GET_EPOCH_VAL(isReclaimerSupportObjMetaData);

//     // TRACE COUTATOMICTID("allocator_new::=created obj"<<(T*)block<<"with birth epoch"<<(*birth_epoch)<<std::endl);

//     // return here
//     return (T*)block; //remeber to placement construct this node in DS for initializing the node members.
// #else
#ifdef DAOI_RUSLON_RECLAIMERS
    return nullptr; 
    // for ruslon relaimers malloc does allocation from reclaimer file.
    // invoke this allocate function to just record debug stats.
#else
    return new T; //(T*) malloc(sizeof(T));
#endif
    }

    void deallocate(const int tid, T * const p) {
        // note: allocators perform the actual freeing/deleting, since
        // only they know how memory was allocated.
        // pools simply call deallocate() to request that it is freed.
        // allocators do not invoke pool functions.
        MEMORY_STATS {
            this->debug->addDeallocated(tid, 1);
//            currentAllocatedBytes -= sizeof(T);
        }
#if !defined NO_FREE
#ifdef DAOI_RUSLON_RECLAIMERS
        free( (char*) p); // freeing placement malloced memory 
#else
        delete p;
#endif //DAOI_RUSLON_RECLAIMERS
#endif
    }
    void deallocateAndClear(const int tid, blockbag<T> * const bag) {
#ifdef NO_FREE
        bag->clearWithoutFreeingElements();
#else
        while (!bag->isEmpty()) {
            T* ptr = bag->remove();
            deallocate(tid, ptr);
        }
#endif
    }
    
    void debugPrintStatus(const int tid) {
//        std::cout<</*"thread "<<tid<<" "<<*/"allocated "<<this->debug->getAllocated(tid)<<" objects of size "<<(sizeof(T));
//        std::cout<<" ";
////        this->pool->debugPrintStatus(tid);
//        std::cout<<std::endl;
    }
    
    void initThread(const int tid) {}
    void deinitThread(const int tid) {}
    
    allocator_new(const int numProcesses, debugInfo * const _debug)
            : allocator_interface<T>(numProcesses, _debug) {
        VERBOSE DEBUG std::cout<<"constructor allocator_new"<<std::endl;
    }
    ~allocator_new() {
        VERBOSE DEBUG std::cout<<"destructor allocator_new"<<std::endl;
    }
};

#endif	/* ALLOC_NEW_H */

