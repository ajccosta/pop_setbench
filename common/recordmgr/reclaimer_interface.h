/**
 * C++ record manager implementation (PODC 2015) by Trevor Brown.
 *
 * Copyright (C) 2015 Trevor Brown
 *
 */

#ifndef RECLAIM_INTERFACE_H
#define	RECLAIM_INTERFACE_H

#include "recovery_manager.h"
#include "pool_interface.h"
#include "globals.h"
#include <iostream>
#include <cstdlib>

template <typename T>
struct set_of_bags {
    blockbag<T> * const * const bags;
    const int numBags;
};

template <typename T = void, class Pool = pool_interface<T> >
class reclaimer_interface {
public:
    PAD;
    RecoveryMgr<void *> * recoveryMgr;
    debugInfo * const debug;

    const int NUM_PROCESSES;
    Pool *pool;
//    PAD;

    template<typename _Tp1>
    struct rebind {
        typedef reclaimer_interface<_Tp1, Pool> other;
    };
    template<typename _Tp1, typename _Tp2>
    struct rebind2 {
        typedef reclaimer_interface<_Tp1, _Tp2> other;
    };

    long long getSizeInNodes() { return 0; }
    std::string getSizeString() { return ""; }
    std::string getDetailsString() { return ""; }

    inline static bool quiescenceIsPerRecordType() { return false; }
    inline static bool shouldHelp() { return true; } // FOR DEBUGGING PURPOSES
    inline static bool supportsCrashRecovery() { return false; }
    inline static bool needsSetJmp(){ return false; } //for NBR

    inline bool isProtected(const int tid, T * const obj){ return false; }
    
    inline bool HEProtect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true){
        COUTATOMICTID("reclaimer_interface::HEProtect() is not implemented!"<<std::endl);
        exit(-1);
    }
    inline void HEUnprotect(const int tid){}

    void reserve_slot(int tid, int index, T* node) {
        COUTATOMICTID("reclaimer_interface::reserve_slot() is not implemented!"<<std::endl);
        exit(-1);
    }

    // used for Ruslon's allocs where placement alloc are used.
    // Ruslon reclaimner allocations also need type info in malloc.
    T* instrumentedAlloc(const int tid)
    {
        COUTATOMICTID("reclaimer_interface::instrumentedAlloc() is not implemented!"<<std::endl);
        exit(-1);
        return nullptr;
    }

    T* read(const int tid, int idx, std::atomic<T*> &obj)
    {
        COUTATOMICTID("reclaimer_interface::read() is not implemented!"<<std::endl);
        exit(-1);
        return nullptr;
    }

    T* readByPtrToTypeAndPtr(const int tid, int idx, std::atomic<T*> &ptrToObj, T* obj)
    {
        COUTATOMICTID("reclaimer_interface::readByPtrToTypeAndPtr() is not implemented!"<<std::endl);
        exit(-1);
        return nullptr;
    }

    inline bool isQProtected(const int tid, T * const obj);
    inline static bool isQuiescent(const int tid) {
        COUTATOMICTID("reclaimer_interface::isQuiescent(tid) is not implemented!"<<std::endl);
        exit(-1);
    }

    // for hazard pointers (and reference counting)
    inline bool protect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true);
    inline void unprotect(const int tid, T* obj);
    inline bool qProtect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true);
    inline void qUnprotectAll(const int tid);

// for epoch based reclamation (or, more generally, any quiescent state based reclamation)
//    inline long readEpoch();
//    inline long readAnnouncedEpoch(const int tid);
    /**
     * endOp<T> must be idempotent,
     * and must unprotect all objects protected by calls to protectObject<T>.
     * it must NOT unprotect any object protected by a call to
     * protectObjectEvenAfterRestart.
     */
    inline void endOp(const int tid);
    inline bool startOp(const int tid, void * const * const reclaimers, const int numReclaimers, const bool readOnly = false);
    inline void rotateEpochBags(const int tid);
    
    //for tr
    inline void saveForWritePhase(const int tid, T * const record){
        COUTATOMICTID("reclaimer_interface::saveForWritePhase() is not implemented!"<<std::endl);
        exit(-1);
    }
    inline void upgradeToWritePhase(const int tid){
        COUTATOMICTID("reclaimer_interface::upgradeToWritePhase(tid) is not implemented!"<<std::endl);
        exit(-1);
    }

    // for all schemes except reference counting
    inline void retire(const int tid, T* p);

    inline void initThread(const int tid);
    inline void deinitThread(const int tid);
    void debugPrintStatus(const int tid);

    template <typename First, typename... Rest>
    void debugGCSingleThreaded(void * const * const reclaimers, const int numReclaimers) {
        // do nothing unless function is replaced
    }

#if defined (OOI_IBR_RECLAIMERS) || defined (DAOI_IBR_RECLAIMERS) || defined (OOI_POP_RECLAIMERS) || defined (IBR_RCU_HP_POP_RECLAIMERS) // for qsbr, rcu, ibr2ge, he_nbr need alloc counter updation
    inline void updateAllocCounterAndEpoch(const int tid){}
    inline uint64_t getEpoch(){ assert(0 && "should only be called by IBR algo requiring birth epoch in ds"); return 0;}
#endif

    reclaimer_interface(const int numProcesses, Pool *_pool, debugInfo * const _debug, RecoveryMgr<void *> * const _recoveryMgr = NULL)
            : recoveryMgr(_recoveryMgr)
            , debug(_debug)
            , NUM_PROCESSES(numProcesses)
            , pool(_pool) {
        VERBOSE DEBUG COUTATOMIC("constructor reclaimer_interface"<<std::endl);
    }
    ~reclaimer_interface() {
        VERBOSE DEBUG COUTATOMIC("destructor reclaimer_interface"<<std::endl);
    }
};

#endif
