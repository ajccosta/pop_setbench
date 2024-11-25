/**
 * The code is adapted from https://github.com/urcs-sync/Interval-Based-Reclamation to make it work with Setbench.
 * This file implements QSBR memory reclamation, mentioned in original Interval Based Memory Reclamation paper, PPOPP 2018.
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

#ifndef RECLAIM_QSBR_H
#define RECLAIM_QSBR_H

#include <list>
#include "ConcurrentPrimitives.h"
#include "blockbag.h"

// #if !defined QSBR_ORIGINAL_FREE || !QSBR_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_qsbr : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int empty_freq;
    int epoch_freq;
    PAD;
    class ThreadData
    {
    private:
        PAD;
    public:
    #ifdef DEAMORTIZE_FREE_CALLS
        blockbag<T> * deamortizedFreeables;
        int numFreesPerStartOp;
    #endif        
    ThreadData()
    {
    }
    private:
        PAD;
    };

    PAD;
    ThreadData threadData[MAX_THREADS_POW2];
    PAD;


public:
    class QSBRInfo
    {
    public:
        T *obj;
        uint64_t epoch;
        QSBRInfo(T *obj, uint64_t epoch) : obj(obj), epoch(epoch) {}
    };

private:
    paddedAtomic<uint64_t> *reservations;
    padded<std::list<QSBRInfo>> *retired;
    padded<uint64_t> *retire_counters;
    padded<uint64_t> *alloc_counters;

    std::atomic<uint64_t> epoch;
    PAD;

public:
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_qsbr<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_qsbr<_Tp1, _Tp2> other;
    };

    inline static bool quiescenceIsPerRecordType() { return false; }

    template <typename First, typename... Rest>
    inline bool startOp(const int tid, void *const *const reclaimers, const int numReclaimers, const bool readOnly = false)
    {
    #ifdef DEAMORTIZE_FREE_CALLS
        // free one object
        if (!threadData[tid].deamortizedFreeables->isEmpty()) {
            this->pool->add(tid, threadData[tid].deamortizedFreeables->remove());
        }
    #endif
    
        return true;
    }

    inline void endOp(const int tid)
    {
        uint64_t e = epoch.load(std::memory_order_acquire);
        reservations[tid].ui.store(e, std::memory_order_seq_cst);

        #ifdef GARBAGE_BOUND_EXP
            if (tid == 1)
            {
                // COUTATOMICTID("reserved e and sleep =" <<e <<std::endl);
                std::this_thread::sleep_for(std::chrono::seconds(40));
            }
        #endif        
    }

    inline void updateAllocCounterAndEpoch(const int tid)
    {
		alloc_counters[tid]=alloc_counters[tid]+1;
		if(alloc_counters[tid]%(epoch_freq*num_process)==0){
			epoch.fetch_add(1,std::memory_order_acq_rel);
		}
    }

    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
        if (obj == NULL)
        {
            return;
        }
        std::list<QSBRInfo> *myTrash = &(retired[tid].ui);
        // for(auto it = myTrash->begin(); it!=myTrash->end(); it++){
        // 	assert(it->obj!=obj && "double retire error");
        // }

        uint64_t e = epoch.load(std::memory_order_acquire);
        QSBRInfo info = QSBRInfo(obj, e);
        myTrash->push_back(info);
        retire_counters[tid] = retire_counters[tid] + 1;
        if (retire_counters[tid] % empty_freq == 0)
        {
            empty(tid);
// #ifdef GSTATS_HANDLE_STATS
//             GSTATS_ADD(tid, num_signal_events, 1);
// #endif        
        }
    }

    void debugPrintStatus(const int tid)
    {
        if (tid == 0)
        {
            std::cout << "global_epoch_counter=" << epoch.load(std::memory_order_acquire) << std::endl;
        }
    }

    void empty(const int tid)
    {
        uint64_t minEpoch = UINT64_MAX;
        for (int i = 0; i < num_process; i++)
        {
            uint64_t res = reservations[i].ui.load(std::memory_order_acquire);
            if (res < minEpoch)
            {
                minEpoch = res;
            }
        }
        // erase safe objects
        std::list<QSBRInfo> *myTrash = &(retired[tid].ui);

        uint before_sz = myTrash->size();
        // COUTATOMICTID("decided to empty! bag size=" << myTrash->size() << std::endl);

        for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;)
        {
            QSBRInfo res = *iterator;
            if (res.epoch < minEpoch)
            {
                iterator = myTrash->erase(iterator); //return iterator corresponding to next of last erased item
                // this->pool->add(tid, res.obj); //reclaim

            #ifdef DEAMORTIZE_FREE_CALLS
                threadData[tid].deamortizedFreeables->add(res.obj);
            #else
                this->pool->add(tid, res.obj); //reclaim
            #endif

            }
            else //skip reclaiming since retired epoch is not safe
            {
                ++iterator;
            }
        }

        uint after_sz = myTrash->size();
        TRACE COUTATOMICTID("After empty! bag size=" << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl << std::endl);
    }

    //dummy declaration
    void initThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = new blockbag<T>(tid, this->pool->blockpools[tid]);
        threadData[tid].numFreesPerStartOp = 1;
#endif
    }
    void deinitThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif

    }
    inline static bool isProtected(const int tid, T *const obj) { return true; }
    inline static bool isQProtected(const int tid, T *const obj) { return false; }
    inline static bool protect(const int tid, T *const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) { return true; }
    inline static void unprotect(const int tid, T *const obj) {}
    inline static bool qProtect(const int tid, T *const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) { return true; }
    inline static void qUnprotectAll(const int tid) {}

    inline uint64_t getEpoch()
    {
        return epoch.load(std::memory_order_acquire);
    }

    reclaimer_qsbr(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_qsbr helping=" << this->shouldHelp() << std::endl; // scanThreshold="<<scanThreshold<<std::endl;
        num_process = numProcesses;
        empty_freq = 16384; //30; //this was default values ion IBR microbench
        epoch_freq = 150;   //this was default values ion IBR microbench

        retired = new padded<std::list<QSBRInfo>>[numProcesses];
        reservations = new paddedAtomic<uint64_t>[numProcesses];
        retire_counters = new padded<uint64_t>[numProcesses];
        alloc_counters = new padded<uint64_t>[numProcesses];

        for (int i = 0; i < numProcesses; i++)
        {
            reservations[i].ui.store(UINT64_MAX, std::memory_order_release);
            retired[i].ui.clear();
        }
        epoch.store(0, std::memory_order_release);

    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif
    }
    ~reclaimer_qsbr()
    {
        for (int i = 0; i < num_process; i++)
        {
            // COUTATOMIC(retired[i].ui.size()<<std::endl);
            for (auto iterator = retired[i].ui.begin(), end = retired[i].ui.end(); iterator != end; )
            {
                QSBRInfo res = *iterator;
                iterator=retired[i].ui.erase(iterator); //return iterator corresponding to next of last erased item
                this->pool->add(i, res.obj); //reclaim
            }

        }        
        
        delete [] retired;
        delete [] reservations;
        delete [] retire_counters;
        delete [] alloc_counters;    
		COUTATOMIC("epoch_freq= " << epoch_freq << "empty_freq= " << empty_freq <<std::endl);
    }
};

#endif //RECLAIM_QSBR_H