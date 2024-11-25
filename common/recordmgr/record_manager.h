/**
 * C++ record manager implementation (PODC 2015) by Trevor Brown.
 *
 * Copyright (C) 2015 Trevor Brown
 *
 */

#ifndef RECORD_MANAGER_H
#define	RECORD_MANAGER_H

#include <atomic>
#include "globals.h"
#include "errors.h"
#include "record_manager_single_type.h"

#include <iostream>
#include <exception>
#include <stdexcept>
#include <typeinfo>

inline CallbackReturn callbackReturnTrue(CallbackArg arg) {
    return true;
}

// compile time check for duplicate template parameters
// compare first with rest to find any duplicates
template <typename T> void check_duplicates(void) {}
template <typename T, typename First, typename... Rest>
void check_duplicates(void) {
    if (typeid(T) == typeid(First)) {
        throw std::logic_error("duplicate template arguments provided to RecordManagerSet");
    }
    check_duplicates<T, Rest...>();
}

// base case: empty template
// this is a compile time check for invalid arguments
template <class Reclaim, class Alloc, class Pool, typename... Rest>
class RecordManagerSet {
    PAD;
public:
    RecordManagerSet(const int numProcesses, RecoveryMgr<void *> * const _recoveryMgr) {}
    template <typename T>
    record_manager_single_type<T, Reclaim, Alloc, Pool> * get(T * const recordType) {
        throw std::logic_error("invalid type passed to RecordManagerSet::get()");
        return NULL;
    }
    void clearCounters(void) {}
    void registerThread(const int tid) {}
    void unregisterThread(const int tid) {}
    void printStatus() {}

    inline void HEUnprotect(const int tid){}
    inline void qUnprotectAll(const int tid) {}
    inline void getReclaimers(const int tid, void ** const reclaimers, int index) {}
    inline void endOp(const int tid) {}
    inline void leaveQuiescentStateForEach(const int tid, const bool readOnly = false) {}
    inline void startOp(const int tid, const bool callForEach, const bool readOnly = false) {}

    inline void debugGCSingleThreaded() {
        printf("DEBUG: record_manager::debugGCSingleThreaded()\n");
    }
};

// "recursive" case
template <class Reclaim, class Alloc, class Pool, typename First, typename... Rest>
class RecordManagerSet<Reclaim, Alloc, Pool, First, Rest...> : RecordManagerSet<Reclaim, Alloc, Pool, Rest...> {
    PAD;
    record_manager_single_type<First, Reclaim, Alloc, Pool> * const mgr;
	PAD;
public:
    RecordManagerSet(const int numProcesses, RecoveryMgr<void *> * const _recoveryMgr)
        : RecordManagerSet<Reclaim, Alloc, Pool, Rest...>(numProcesses, _recoveryMgr)
        , mgr(new record_manager_single_type<First, Reclaim, Alloc, Pool>(numProcesses, _recoveryMgr))
        {
        //cout<<"RecordManagerSet with First="<<typeid(First).name()<<" and sizeof...(Rest)="<<sizeof...(Rest)<<std::endl;
        check_duplicates<First, Rest...>(); // check if first is in {rest...}
    }
    ~RecordManagerSet() {
        std::cout<<"recordmanager set destructor started for object type "<<typeid(First).name()<<std::endl;
        delete mgr;
        std::cout<<"recordmanager set destructor finished for object type "<<typeid(First).name()<<std::endl;
        // note: should automatically call the parent class' destructor afterwards
    }
    // note: the compiled code for get() should be a single read and return statement
    template<typename T>
    inline record_manager_single_type<T, Reclaim, Alloc, Pool> * get(T * const recordType) {
        if (typeid(First) == typeid(T)) {
//            std::cout<<"MATCH: typeid(First)="<<typeid(First).name()<<" typeid(T)="<<typeid(T).name()<<std::endl;
            return (record_manager_single_type<T, Reclaim, Alloc, Pool> *) mgr;
        } else {
//            std::cout<<"NO MATCH: typeid(First)="<<typeid(First).name()<<" typeid(T)="<<typeid(T).name()<<std::endl;
            return ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->get(recordType);
        }
    }
    // note: recursion should be compiled out
    void clearCounters(void) {
        mgr->clearCounters();
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->clearCounters();
    }
    void registerThread(const int tid) {
        mgr->initThread(tid);
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->registerThread(tid);
    }
    void unregisterThread(const int tid) {
        mgr->deinitThread(tid);
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->unregisterThread(tid);
    }
    void printStatus() {
        mgr->printStatus();
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->printStatus();
    }
    inline void qUnprotectAll(const int tid) {
        mgr->qUnprotectAll(tid);
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->qUnprotectAll(tid);
    }

    inline void HEUnprotect(const int tid) {
        mgr->HEUnprotect(tid);
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->HEUnprotect(tid);
    }

    
    inline void getReclaimers(const int tid, void ** const reclaimers, int index) {
        reclaimers[index] = mgr->reclaim;
        ((RecordManagerSet <Reclaim, Alloc, Pool, Rest...> *) this)->getReclaimers(tid, reclaimers, 1+index);
    }
    inline void endOp(const int tid) { //@J for tr I have endOP currently perrecord type. Is it okay to be signalled between this recursion for per record type?
        mgr->endOp(tid);
        ((RecordManagerSet<Reclaim, Alloc, Pool, Rest...> *) this)->endOp(tid);
    }
    inline void leaveQuiescentStateForEach(const int tid, const bool readOnly = false) {
        mgr->template startOp<First, Rest...>(tid, NULL, 0, readOnly);
        ((RecordManagerSet <Reclaim, Alloc, Pool, Rest...> *) this)->leaveQuiescentStateForEach(tid, readOnly);
    }
    inline void startOp(const int tid, const bool callForEach, const bool readOnly = false) {
        if (callForEach) {
            leaveQuiescentStateForEach(tid, readOnly);
        } else {
            void * reclaimers[1+sizeof...(Rest)];
            getReclaimers(tid, reclaimers, 0);
            get((First *) NULL)->template startOp<First, Rest...>(tid, reclaimers, 1+sizeof...(Rest), readOnly);
            __sync_synchronize(); // memory barrier needed (only) for epoch based schemes at the moment...
        }
    }

    inline void debugGCSingleThreaded() {
        printf("DEBUG: record_manager::debugGCSingleThreaded() 1+sizeof...(Rest)=%lu\n", 1+sizeof...(Rest));
        void * reclaimers[1+sizeof...(Rest)];
        getReclaimers(0, reclaimers, 0);
        get((First *) NULL)->template debugGCSingleThreaded<First, Rest...>(reclaimers, 1+sizeof...(Rest));
        __sync_synchronize(); // memory barrier needed (only) for epoch based schemes at the moment...
    }
};

template <class Reclaim, class Alloc, class Pool, typename First, typename... Rest>
class RecordManagerSetPostPadded : public RecordManagerSet<Reclaim, Alloc, Pool, First, Rest...> {
    PAD;
public:
    RecordManagerSetPostPadded(const int numProcesses, RecoveryMgr<void *> * const _recoveryMgr)
        : RecordManagerSet<Reclaim, Alloc, Pool, First, Rest...>(numProcesses, _recoveryMgr)
    {}
};

template <class Reclaim, class Alloc, class Pool, typename RecordTypesFirst, typename... RecordTypesRest>
class record_manager {
protected:
    typedef record_manager<Reclaim,Alloc,Pool,RecordTypesFirst,RecordTypesRest...> SelfType;
    PAD;
    RecordManagerSetPostPadded<Reclaim,Alloc,Pool,RecordTypesFirst,RecordTypesRest...> * rmset;

//    PAD;
    int init[MAX_THREADS_POW2] = {0,};

public:
//    PAD;
    const int NUM_PROCESSES;
    RecoveryMgr<SelfType> * const recoveryMgr;
    PAD;

    record_manager(const int numProcesses, const int _neutralizeSignal = -1 /* unused except in conjunction with special DEBRA+ memory reclamation */)
            : NUM_PROCESSES(numProcesses)
            , recoveryMgr(new RecoveryMgr<SelfType>(numProcesses, _neutralizeSignal, this))
    {
        rmset = new RecordManagerSetPostPadded<Reclaim, Alloc, Pool, RecordTypesFirst, RecordTypesRest...>(numProcesses, (RecoveryMgr<void *> *) recoveryMgr);
    }
    ~record_manager() {
            delete recoveryMgr;
            delete rmset;
    }
    void initThread(const int tid) {
        AJDBG COUTATOMICTID("recmgr::initThread pthreadself:"<<pthread_self()<<std::endl);
        recoveryMgr->initThread(tid);
        if (init[tid]) return; else init[tid] = !init[tid];

        rmset->registerThread(tid); //@J moved up, think. other initThread() donot map pid with tid so main thread hijacking tid 0 is okay. As another thread 
        //with tid 0 take sover. But recovery mgr maps pid so not OK as main pid is unique and another tid would never be able to access 0th slot.
//        recoveryMgr->initThread(tid);
//        endOp(tid);
    }
    void deinitThread(const int tid) {
        AJDBG COUTATOMICTID("recmgr::deinitThread pthreadself:"<<pthread_self()<<std::endl);
        recoveryMgr->deinitThread(tid); //@J moved up, think. other initThread() donot map pid with tid so main thread hijacking tid 0 is okay. As another thread 
        //with tid 0 take sover. But recovery mgr maps pid so not OK as main pid is unique and another tid would never be able to access 0th slot.
        if (!init[tid]) return; else init[tid] = !init[tid];
//        recoveryMgr->deinitThread(tid);
        rmset->unregisterThread(tid);
    }
    void clearCounters() {
        rmset->clearCounters();
    }
    void printStatus(void) {
        rmset->printStatus();
    }
    template <typename T>
    debugInfo * getDebugInfo(T * const recordType) {
        return &rmset->get((T *) NULL)->debugInfoRecord;
    }
    template <typename T>
    inline record_manager_single_type<T, Reclaim, Alloc, Pool> * get(T * const recordType) {
        return rmset->get((T *) NULL);
    }

    // for hazard pointers
    template <typename T>
    inline bool isProtected(const int tid, T * const obj) {
        return rmset->get((T *) NULL)->isProtected(tid, obj);
    }

    template <typename T>
    inline bool protect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return rmset->get((T *) NULL)->protect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }

    template <typename T>
    inline void unprotect(const int tid, T * const obj) {
        rmset->get((T *) NULL)->unprotect(tid, obj);
    }

    template <typename T>
    inline bool HEProtect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return rmset->get((T *) NULL)->HEProtect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }

    template <typename T>
    void reserve_slot(int tid, int index, T* obj) {
        return rmset->get((T *) NULL)->reserve_slot(tid, index, obj);
    }
        
    inline void HEUnprotect(const int tid) {
        rmset->HEUnprotect(tid);
    }
    
    inline void transfer(const int tid, int src_ix, int dst_ix) {
        rmset->get((RecordTypesFirst *) NULL)->transfer(tid, src_ix, dst_ix);
    }


    // for DEBRA+

    // warning: qProtect must be reentrant and lock-free (i.e., async-signal-safe)
    template <typename T>
    inline bool qProtect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool hintMemoryBarrier = true) {
        return rmset->get((T *) NULL)->qProtect(tid, obj, notRetiredCallback, callbackArg, hintMemoryBarrier);
    }

    template <typename T>
    inline bool isQProtected(const int tid, T * const obj) {
        return rmset->get((T *) NULL)->isQProtected(tid, obj);
    }

    // used for Ruslon's allocs where placement alloc are used.
    // Ruslon reclaimner allocations also need type info in malloc.
    template <typename T>
    T* instrumentedAlloc(const int tid)
    {
        return rmset->get((T *) NULL)->instrumentedAlloc(tid);
    }

    template <typename T>
    T* read(const int tid, int idx, std::atomic<T*> &obj)
    {
        return rmset->get((T *) NULL)->read(tid, idx, obj);
    }

    template <typename T>
    T* readByPtrToTypeAndPtr(const int tid, int idx, std::atomic<T*> &ptrToObj, T* obj)
    {
        return rmset->get((T *) NULL)->readByPtrToTypeAndPtr(tid, idx, ptrToObj, obj);
    }

    inline void qUnprotectAll(const int tid) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        rmset->qUnprotectAll(tid);
    }

    // for epoch based reclamation
    inline bool isQuiescent(const int tid) {
        return rmset->get((RecordTypesFirst *) NULL)->isQuiescent(tid); // warning: if quiescence information is logically shared between all types, with the actual data being associated only with the first type (as it is here), then isQuiescent will return inconsistent results if called in functions that recurse on the template argument list in this class.
    }

    // for pop reclamation
    inline void publishReservations(const int tid) {
        rmset->get((RecordTypesFirst *) NULL)->publishReservations(tid);
    }
    inline void endOp(const int tid) {
//        VERBOSE DEBUG2 COUTATOMIC("record_manager_single_type::endOp(tid="<<tid<<")"<<std::endl);
        //@J using setjmp here would allow to call endOP for each record type. Thus will reset the the proposedHzPtr[] bag
        //for each type. Also their seems to be no harm is setting restartable = 0 again for each record type. As reclaimer 
        //would not restart(siglongjmp) this thread whihc might be trying to call endOP for other record type.
        if (Reclaim::quiescenceIsPerRecordType() /*|| needsSetJmp() * using templated startOP to free HzBag as well as ret bag*/) {
//            std::cout<<"setting quiescent state for all record types\n";
            rmset->endOp(tid);
        } else {
            // only call endOp for one object type
//            std::cout<<"setting quiescent state for just one record type: "<<typeid(RecordTypesFirst).name()<<"\n";
            rmset->get((RecordTypesFirst *) NULL)->endOp(tid);
        }
    }
    
    //@J cannot have per record type startOp for tr because it sets restartable =1. Once in first call restartable =1 .
    //other reclaimer thread may restart the thread which is in between calling the startOp for second type. May lead to undefined behaviour
    //due to longjmps in restart.
    inline void startOp(const int tid, const bool readOnly = false) {
//        assert(isQuiescent(tid));
//        VERBOSE DEBUG2 COUTATOMIC("record_manager_single_type::startOp(tid="<<tid<<")"<<std::endl);
        // for some types of reclaimers, different types of records retired in the same
        // epoch can be reclaimed together (by aggregating their epochs), so we don't actually need
        // separate calls to startOp for each object type.
        // if appropriate, we make a single call to startOp,
        // and it takes care of all record types managed by this record manager.
//        std::cout<<"quiescenceIsPerRecordType = "<<Reclaim::quiescenceIsPerRecordType()<<std::endl;
        rmset->startOp(tid, Reclaim::quiescenceIsPerRecordType(), readOnly);
    }

    // for all schemes
    template <typename T>
    inline void retire(const int tid, T * const p) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        rmset->get((T *) NULL)->retire(tid, p);
    }
    
    /**
     * Similar to qProtect
     * @param tid
     * @param record
     */
    template <typename T>
    inline void saveForWritePhase(const int tid, T * const record){
//        std::cout<<" typeid(T)="<<typeid(T).name()<<std::endl;
        rmset->get((T *) NULL)->saveForWritePhase(tid, record); //@J NOTICEME: DO I need per record type call here? May be not
    }
    inline void upgradeToWritePhase(const int tid){
        rmset->get((RecordTypesFirst *) NULL)->upgradeToWritePhase(tid); //@J NOTICEME: Not needed per record type.
    }

    template <typename T>
    inline T * allocate(const int tid) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
//        GSTATS_ADD_IX(tid, num_prop_epoch_allocations, 1, GSTATS_GET(tid, thread_announced_epoch));
        return rmset->get((T *) NULL)->allocate(tid);
    }

    // optional function which can be used if it is safe to call free()
    template <typename T>
    inline void deallocate(const int tid, T * const p) {
        assert(!Reclaim::supportsCrashRecovery() || isQuiescent(tid));
        rmset->get((T *) NULL)->deallocate(tid, p);
    }

    inline static bool shouldHelp() { // FOR DEBUGGING PURPOSES
        return Reclaim::shouldHelp();
    }
    inline static bool supportsCrashRecovery() {
        return Reclaim::supportsCrashRecovery();
    }    
    inline static bool needsSetJmp(){
        return Reclaim::needsSetJmp();
    }

#if defined (OOI_IBR_RECLAIMERS) || defined (DAOI_IBR_RECLAIMERS) || defined (OOI_POP_RECLAIMERS) || defined (IBR_RCU_HP_POP_RECLAIMERS) // for qsbr, rcu, ibr2ge, he_nbr need alloc counter updation
    
    inline void updateAllocCounterAndEpoch(const int tid)
    {
        rmset->get((RecordTypesFirst *) NULL)->updateAllocCounterAndEpoch(tid);
    }
    inline uint64_t getEpoch()
    {
        return rmset->get((RecordTypesFirst *) NULL)->getEpoch();
    }
#endif
    
    class MemoryReclamationGuard {
        const int tid;
        record_manager<Reclaim, Alloc, Pool, RecordTypesFirst, RecordTypesRest...> * recmgr;
    public:
        MemoryReclamationGuard(const int _tid, record_manager<Reclaim, Alloc, Pool, RecordTypesFirst, RecordTypesRest...> * _recmgr, const bool readOnly = false)
        : tid(_tid), recmgr(_recmgr) {
            recmgr->startOp(tid, readOnly);
        }
        ~MemoryReclamationGuard() {
            recmgr->endOp(tid);
        }
        void end() {
            recmgr->endOp(tid);
        }
    };

    inline MemoryReclamationGuard getGuard(const int tid, const bool readOnly = false) {
        SOFTWARE_BARRIER;
        return MemoryReclamationGuard(tid, this, readOnly);
    }

    void debugGCSingleThreaded() {
        rmset->debugGCSingleThreaded();
    }
};

#endif
