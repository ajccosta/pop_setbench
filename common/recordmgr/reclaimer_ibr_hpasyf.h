/**
 * The code is adapted from https://github.com/urcs-sync/Interval-Based-Reclamation to make it work with Setbench.
 * This file implements HE memory reclamation (in original Interval Based Memory Reclamation paper, PPOPP 2018).
 * The exact file for 2geibr is RangeTrackerNew.hpp in the IBR code.
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

#ifndef RECLAIM_HPASYF_H
#define RECLAIM_HPASYF_H

#include <list>
#include "ConcurrentPrimitives.h"
#include "blockbag.h"

// #if !defined HP_ORIGINAL_FREE || !HP_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_ibr_hpasyf : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int empty_freq;
    int slotsPerThread;
    // PAD;
    paddedAtomic<T*> *slots;
    padded<int> *cntrs;
    padded<std::list<T*>> *retired;
    // PAD;

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

    // PAD;
    ThreadData threadData[MAX_THREADS_POW2];
    PAD;

public:
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_ibr_hpasyf<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_ibr_hpasyf<_Tp1, _Tp2> other;
    };

    template <typename First, typename... Rest>
    inline bool startOp(const int tid, void *const *const reclaimers, const int numReclaimers, const bool readOnly = false)
    {
        bool result = true;
    #ifdef DEAMORTIZE_FREE_CALLS
        // free one object
        if (!threadData[tid].deamortizedFreeables->isEmpty()) {
            this->pool->add(tid, threadData[tid].deamortizedFreeables->remove());
        }
    #endif
        return result;
    }

    inline void endOp(const int tid)
    {
        for(int i = 0; i<slotsPerThread; i++){
			slots[tid*slotsPerThread+i] = NULL;
		}
    }

    // hp ds doesnt need to use this func
    // inline void updateAllocCounterAndEpoch(const int tid)
    // {
	// 	alloc_counters[tid]=alloc_counters[tid]+1;
	// 	if(alloc_counters[tid]%(epoch_freq*num_process)==0){
	// 		epoch.fetch_add(1,std::memory_order_acq_rel);
    //         // COUTATOMICTID("epoch="<<epoch.load(std::memory_order_acquire)<<std::endl);
	// 	}
    //     // COUTATOMICTID("epoch="<<epoch.load(std::memory_order_acquire)<<std::endl);

    // }

    // inline uint64_t getEpoch()
    // {
    //     return epoch.load(std::memory_order_acquire);
    // }

    /**Inner utility method for protect* idx is only used hazard era*/
    T* read(int tid, int idx, std::atomic<T*> &obj)
    {
        #ifdef GARBAGE_BOUND_EXP
            if (tid == 1)
            {
                // COUTATOMICTID("reserved e and sleep =" <<e <<std::endl);
                std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DELAY));
            }
        #endif                
		T* ret;
		T* realptr;
		while(true){
			ret = obj.load(std::memory_order_relaxed);
			realptr = (T*)((size_t)ret & 0xfffffffffffffffc);
			reserve(realptr, idx, tid);
            asm volatile ("" : : : "memory");
			if(ret == obj.load(std::memory_order_acquire)){
				return ret;
			}
		}
    }

    inline void reserve(T* ptr, int slot, int tid){
		// slots[tid*slotsPerThread+slot] = ptr;
        // using relaxed which is best case for perf of asym fence
        slots[tid*slotsPerThread+slot].ui.store(ptr, std::memory_order_relaxed);
	}

    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
        if (obj == NULL)
        {
            return;
        }
        std::list<T*> *myTrash = &(retired[tid].ui);
        myTrash->push_back(obj);

		if(cntrs[tid] == empty_freq){
            // if(myTrash->size() >= empty_freq){
			cntrs[tid] = 0;

            // #ifdef GSTATS_HANDLE_STATS
            //         GSTATS_ADD(tid, num_signal_events, 1);
            // #endif        

			empty(tid);
		}

        if(cntrs[tid].ui% (1000) == 0){
        #ifdef USE_GSTATS
            GSTATS_APPEND(tid, reclamation_event_size, myTrash->size());
        #endif            
        }

		cntrs[tid].ui++;
    }

    inline bool conflict(T** scanned_objs, T* ptr)
    {
        for (int i = 0; i<num_process*slotsPerThread; i++){
            if(ptr == scanned_objs[i]){
                return true;
            }
        }
        return false;
    }

    void empty(const int tid)
    {
		std::list<T*>* myTrash = &(retired[tid].ui);
        uint before_sz = myTrash->size();

        membarrier(MEMBARRIER_CMD_GLOBAL, 0, 0);
        T* temp_local_slots[num_process*slotsPerThread];
        for (int i = 0; i<num_process*slotsPerThread; i++){
            temp_local_slots[i] = slots[i].ui.load(std::memory_order_relaxed);
        }

		for (typename std::list<T*>::iterator iterator = myTrash->begin(), end = myTrash->end(); iterator != end; ) {
			// bool danger = false;
			auto ptr = *iterator;
            
            // if (conflict(temp_local_slots, ptr))
            // {
            //     danger = true;
            // }

			if(! conflict(temp_local_slots, ptr)){
				// this->reclaim(ptr);
                // this->pool->add(tid, ptr);
                #ifdef DEAMORTIZE_FREE_CALLS
                    threadData[tid].deamortizedFreeables->add(ptr);
                #else
                    this->pool->add(tid, ptr); //reclaim
                #endif

				// this->dec_retired(tid);
				iterator = myTrash->erase(iterator);
			}
			else{++iterator;}
		}
        uint after_sz = myTrash->size();
        TRACE COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);

		return;
    }

    void debugPrintStatus(const int tid)
    {
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
    inline static bool quiescenceIsPerRecordType() { return false; }


    /**
     * To escape asserts at record manager assert(!supportcrash || isQuiescent())
    */
    inline static bool isQuiescent(const int tid)
    {
        return true;
    }

    reclaimer_ibr_hpasyf(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_ibr_hpasyf helping=" << this->shouldHelp() << std::endl;
        num_process = numProcesses;
        // empty_freq = 24576; //16384;//32768;//100; //30; // 32K gives best gains for AF version. larger or lower doesn't make a much difference.
#ifdef LONG_RUNNING_EXP
        empty_freq = 2048; //32768; //16384; //32000; //30; //on lines with nbr
#else
        empty_freq = 24576; //16384; //32000; //30; //on lines with nbr
#endif
        slotsPerThread = 3;

        slots = new paddedAtomic< T* >[num_process * slotsPerThread];
        for (int i = 0; i < num_process * slotsPerThread; i++)
        {
            slots[i] = NULL;
        }

        retired = new padded<std::list<T*>>[num_process];
        cntrs = new padded<int>[num_process];

        for (int i = 0; i < num_process; i++)
        {
            cntrs[i] = 0;
            retired[i].ui = std::list<T*> ();
        }
    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif  
        if (init_membarrier())
            exit(EXIT_FAILURE);

    }

    ~reclaimer_ibr_hpasyf()
    {
        delete [] retired;
        delete [] slots;
		COUTATOMIC("empty_freq= " << empty_freq <<std::endl);
    }
};

#endif //RECLAIM_HPASYF_H
