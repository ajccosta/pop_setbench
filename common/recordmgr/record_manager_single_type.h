/**
 * C++ record manager implementation (PODC 2015) by Trevor Brown.
 *
 * Copyright (C) 2015 Trevor Brown
 *
 */

#ifndef RECORD_MANAGER_SINGLE_TYPE_H
#define	RECORD_MANAGER_SINGLE_TYPE_H

#include <pthread.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <typeinfo>

#include "plaf.h"
#include "debug_info.h"
#include "globals.h"

#include "recovery_manager.h"

#include "allocator_interface.h"
#include "allocator_new.h"

#include "pool_interface.h"
#include "pool_none.h"

#include "reclaimer_interface.h"
#include "reclaimer_none.h"

#include "reclaimer_ibr_rcu.h"
#include "reclaimer_qsbr.h"
#include "reclaimer_2geibr.h"
#include "reclaimer_he.h"
#include "reclaimer_nbr_pophe.h"
#include "reclaimer_pop2geibr.h"
#include "reclaimer_popplus2geibr.h"
#include "reclaimer_nbr_popplushe.h"
#include "reclaimer_rcu_pop.h"
#include "reclaimer_rcu_popplus.h"
#include "reclaimer_ibr_hp.h"
#include "reclaimer_ibr_hpasyf.h"
#include "reclaimer_ibr_pophp.h"
#include "reclaimer_ibr_popplushp.h"
#include "reclaimer_rcu_pophp.h"
#include "reclaimer_rcu_popplushp.h"


// aj stopping compiling ruslon recs to use c++14
// #include "reclaimer_crystallineL.h"
// #include "reclaimer_crystallineW.h"
// #include "reclaimer_wfe.h"


#include "reclaimer_debra.h"
#include "reclaimer_nbr.h"
#include "reclaimer_nbrplus.h"
#include "reclaimer_nbr_orig.h"

#include "reclaimer_hazardptr.h"
// #include "reclaimer_debra_dfc.h"
#include "reclaimer_token1.h"
#include "reclaimer_token4.h"

// maybe Record should be a size
template <typename Record, class Reclaim, class Alloc, class Pool>
class record_manager_single_type {
protected:
    typedef Record* record_pointer;

    typedef typename Alloc::template    rebind<Record>::other              classAlloc;
    typedef typename Pool::template     rebind2<Record, classAlloc>::other classPool;
    typedef typename Reclaim::template  rebind2<Record, classPool>::other  classReclaim;

public:
    PAD;
    classAlloc      *alloc;
    classPool       *pool;
    classReclaim    *reclaim;

    const int NUM_PROCESSES;
    debugInfo debugInfoRecord;
    RecoveryMgr<void *> * const recoveryMgr;
    PAD;
    padded<int*>* slot_renamers = NULL;
    PAD;

    record_manager_single_type(const int numProcesses, RecoveryMgr<void *> * const _recoveryMgr)
            : NUM_PROCESSES(numProcesses), debugInfoRecord(debugInfo(numProcesses)), recoveryMgr(_recoveryMgr) {
        VERBOSE DEBUG COUTATOMIC("constructor record_manager_single_type"<<std::endl);
        alloc = new classAlloc(numProcesses, &debugInfoRecord);
        pool = new classPool(numProcesses, alloc, &debugInfoRecord);
        reclaim = new classReclaim(numProcesses, pool, &debugInfoRecord, recoveryMgr);

        slot_renamers = new padded<int*>[numProcesses];
		for (int i = 0; i < numProcesses; i++){
			slot_renamers[i].ui = new int[32];
			for (int j = 0; j < 32; j++){
				slot_renamers[i].ui[j] = j;
			}
		}
    }
    ~record_manager_single_type() {
        VERBOSE DEBUG COUTATOMIC("destructor record_manager_single_type"<<std::endl);
        delete reclaim;
        delete pool;
        delete alloc;
		for (int i = 0; i < NUM_PROCESSES; i++){
            delete [] slot_renamers[i].ui;
        }
        delete [] slot_renamers;

    }

    void initThread(const int tid) {
        alloc->initThread(tid);
        pool->initThread(tid);
        reclaim->initThread(tid);
//        endOp(tid);
    }

    void deinitThread(const int tid) {
        reclaim->deinitThread(tid);
        pool->deinitThread(tid);
        alloc->deinitThread(tid);
    }

    inline void clearCounters() {
        debugInfoRecord.clear();
    }

    inline static bool shouldHelp() { // FOR DEBUGGING PURPOSES
        return Reclaim::shouldHelp();
    }
    inline bool isProtected(const int tid, record_pointer obj) {
        return reclaim->isProtected(tid, obj);
    }

    // for hazard pointers (and reference counting)
    inline bool protect(const int tid, record_pointer obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return reclaim->protect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }
    inline void unprotect(const int tid, record_pointer obj) {
        reclaim->unprotect(tid, obj);
    }

    inline bool HEProtect(const int tid, record_pointer obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return reclaim->HEProtect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }

    void reserve_slot(int tid, int index, record_pointer node) {
        return reclaim->reserve_slot(tid, index, node);
    }

    inline void transfer(const int tid, int src_ix, int dst_ix) {
		int tmp = slot_renamers[tid].ui[src_ix];
		slot_renamers[tid].ui[src_ix] = slot_renamers[tid].ui[dst_ix];
		slot_renamers[tid].ui[dst_ix] = tmp;
    }

    inline void HEUnprotect(const int tid) {
        reclaim->HEUnprotect(tid);
    }


    // warning: qProtect must be reentrant and lock-free (=== async-signal-safe)
    inline bool qProtect(const int tid, record_pointer obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return reclaim->qProtect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }
    inline void qUnprotectAll(const int tid) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        reclaim->qUnprotectAll(tid);
    }
    inline bool isQProtected(const int tid, record_pointer obj) {
        return reclaim->isQProtected(tid, obj);
    }
    
    record_pointer instrumentedAlloc(const int tid)
    {
        return reclaim->instrumentedAlloc(tid);
    }

    record_pointer read(const int tid, int idx, std::atomic<record_pointer> &obj)
    {
        return reclaim->read(tid, slot_renamers[tid].ui[idx], obj);
    }

    record_pointer readByPtrToTypeAndPtr(const int tid, int idx, std::atomic<record_pointer> &ptrToObj, record_pointer obj)
    {
        return reclaim->readByPtrToTypeAndPtr(tid, slot_renamers[tid].ui[idx], ptrToObj, obj);
    }

    inline static bool supportsCrashRecovery() {
        return Reclaim::supportsCrashRecovery();
    }
    inline static bool needsSetJmp() {
        return Reclaim::needsSetJmp();
    }
    
    inline static bool quiescenceIsPerRecordType() {
        return Reclaim::quiescenceIsPerRecordType();
    }
    inline bool isQuiescent(const int tid) {
        return reclaim->isQuiescent(tid);
    }

    inline void publishReservations(const int tid) {
        reclaim->publishReservations(tid);
    }

    // for epoch based reclamation
    inline void endOp(const int tid) {
//        VERBOSE DEBUG2 COUTATOMIC("record_manager_single_type::endOp(tid="<<tid<<")"<<std::endl);
        reclaim->endOp(tid);
    }
    template <typename First, typename... Rest>
    inline void startOp(const int tid, void * const * const reclaimers, const int numReclaimers, const bool readOnly = false) {
//        assert(isQuiescent(tid));
        reclaim->template startOp<First, Rest...>(tid, reclaimers, numReclaimers, readOnly);
    }

    template <typename First, typename... Rest>
    inline void debugGCSingleThreaded(void * const * const reclaimers, const int numReclaimers) {
        reclaim->template debugGCSingleThreaded<First, Rest...>(reclaimers, numReclaimers);
    }

    // for all schemes except reference counting
    inline void retire(const int tid, record_pointer p) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        reclaim->retire(tid, p);
    }
    
    //for tr
//    template <typename T>
    inline void saveForWritePhase(const int tid, record_pointer record){
        reclaim->saveForWritePhase(tid, record);
    }
    inline void upgradeToWritePhase(const int tid){
        reclaim->upgradeToWritePhase(tid);
    }
    
    
    // for all schemes
    inline record_pointer allocate(const int tid) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        return pool->get(tid);
    }

    inline void deallocate(const int tid, record_pointer p) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        pool->add(tid, p);
    }

#if defined (OOI_IBR_RECLAIMERS) || defined (DAOI_IBR_RECLAIMERS) || defined (OOI_POP_RECLAIMERS) || defined (IBR_RCU_HP_POP_RECLAIMERS)// for qsbr, rcu, ibr2ge he_nbr need alloc counter updation
    inline void updateAllocCounterAndEpoch(const int tid)
    {
        reclaim->updateAllocCounterAndEpoch(tid);
    }

    inline uint64_t getEpoch()
    {
        return reclaim->getEpoch();
    }
#endif

    void printStatus(void) {
        long long allocated = debugInfoRecord.getTotalAllocated();
        long long allocatedBytes = allocated * sizeof(Record);
        long long deallocated = debugInfoRecord.getTotalDeallocated();
        long long getFromPool = debugInfoRecord.getTotalFromPool(); // - allocated;

//        COUTATOMIC("recmgr status for objects of size "<<sizeof(Record)<<" and type "<<typeid(Record).name()<<std::endl);
//        COUTATOMIC("allocated   : "<<allocated<<" objects totaling "<<allocatedBytes<<" bytes ("<<(allocatedBytes/1000000.)<<"MB)"<<std::endl);
//        COUTATOMIC("recycled    : "<<recycled<<std::endl);
//        COUTATOMIC("deallocated : "<<deallocated<<" objects"<<std::endl);
//        COUTATOMIC("pool        : "<<pool->getSizeString()<<std::endl);
//        COUTATOMIC("reclaim     : "<<reclaim->getSizeString()<<std::endl);
//        COUTATOMIC("unreclaimed : "<<(allocated - deallocated - atoi(reclaim->getSizeString().c_str()))<<std::endl);
//        COUTATOMIC(std::endl);

        COUTATOMIC(typeid(Record).name()<<"_object_size="<<sizeof(Record)<<std::endl);
        COUTATOMIC(typeid(Record).name()<<"_allocated_count="<<allocated<<std::endl);
        COUTATOMIC(typeid(Record).name()<<"_allocated_size="<<(allocatedBytes/1000000.)<<"MB"<<std::endl);
        COUTATOMIC(typeid(Record).name()<<"_get_from_pool="<<getFromPool<<std::endl);
        COUTATOMIC(typeid(Record).name()<<"_deallocated="<<deallocated<<std::endl);
        COUTATOMIC(typeid(Record).name()<<"_limbo_count="<<reclaim->getSizeString()<<std::endl);
        COUTATOMIC(typeid(Record).name()<<"_limbo_details="<<reclaim->getDetailsString()<<std::endl);
        //COUTATOMIC(typeid(Record).name()<<"_pool_count="<<pool->getSizeString()<<std::endl);
        COUTATOMIC("num_unreclaimed="<<(allocated - deallocated)<<std::endl);
        COUTATOMIC(std::endl);

        for (int tid=0;tid<NUM_PROCESSES;++tid) {
            reclaim->debugPrintStatus(tid);
        }
        COUTATOMIC(std::endl);

//        for (int tid=0;tid<this->NUM_PROCESSES;++tid) {
//            COUTATOMIC("thread "<<tid<<" ");
//            alloc->debugPrintStatus(tid);
//
//            COUTATOMIC("    ");
//            //COUTATOMIC("allocated "<<debugInfoRecord.getAllocated(tid)<<" Nodes");
//            //COUTATOMIC("allocated "<<(debugInfoRecord.getAllocated(tid) / 1000)<<"k Nodes");
//            //COUTATOMIC(" ");
//            reclaim->debugPrintStatus(tid);
//            COUTATOMIC(" ");
//            pool->debugPrintStatus(tid);
//            COUTATOMIC(" ");
//            COUTATOMIC("(given="<<debugInfoRecord.getGiven(tid)<<" taken="<<debugInfoRecord.getTaken(tid)<<") toPool="<<debugInfoRecord.getToPool(tid)<<" fromPool="<<debugInfoRecord.getFromPool(tid));
//            COUTATOMIC(std::endl);
//        }
    }
};

#endif