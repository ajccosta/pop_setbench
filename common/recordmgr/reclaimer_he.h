/**
 * The code is adapted from https://github.com/urcs-sync/Interval-Based-Reclamation to make it work with Setbench.
 * This file implements HE memory reclamation (in original Interval Based Memory Reclamation paper, PPOPP 2018).
 * The exact file for 2geibr is RangeTrackerNew.hpp in the IBR code.
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

#ifndef RECLAIM_HE_H
#define RECLAIM_HE_H

#include <list>
#include "ConcurrentPrimitives.h"
#include "blockbag.h"

// #if !defined HE_ORIGINAL_FREE || !HE_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_he : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int freq;
    int epoch_freq;
    int num_he;
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

    // to save global reservations once before emptying retired bag.
    uint64_t *scannedHEs;

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
    class HeInfo
    {
    public:
        T *obj;
        // birth epoch would come from ds node's field named birth epoch. 
        // this is to avoid doing a bad hack in allocator interface where I would need
        // to silently allocate a node larger than what DS implemntor defines to allocate memory for birth epoch.
        // It is better to declare birth epoch as part of DS implementor. Easy, straightforward and simple over tricky.
        uint64_t birth_epoch; 
        uint64_t retire_epoch;
        HeInfo(T *obj, uint64_t b_epoch, uint64_t r_epoch) : obj(obj), birth_epoch(b_epoch), retire_epoch(r_epoch) {}
    };

private:
    padded< std::atomic<uint64_t>*>* reservations; // per thread array of reservations 
    padded<uint64_t> *retire_counters;
    padded<uint64_t> *alloc_counters;
    padded<std::list<HeInfo>> *retired;

    std::atomic<uint64_t> epoch;
    PAD;

public:
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_he<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_he<_Tp1, _Tp2> other;
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
        for (int i = 0; i < num_he; i++)
        {
			reservations[tid].ui[i].store(0, std::memory_order_seq_cst);
		}
    }

    inline void updateAllocCounterAndEpoch(const int tid)
    {
		alloc_counters[tid]=alloc_counters[tid]+1;
		if(alloc_counters[tid]%(epoch_freq*num_process)==0){
			epoch.fetch_add(1,std::memory_order_acq_rel);
            // COUTATOMICTID("epoch="<<epoch.load(std::memory_order_acquire)<<std::endl);
		}
        // COUTATOMICTID("epoch="<<epoch.load(std::memory_order_acquire)<<std::endl);

    }

    inline uint64_t getEpoch()
    {
        return epoch.load(std::memory_order_acquire);
    }

    /**Inner utility method for protect* idx is only used hazard era*/
    T* read(int tid, int idx, std::atomic<T*> &obj)
    {

		uint64_t prev_epoch = reservations[tid].ui[idx].load(std::memory_order_acquire);
		while(true){
			T* ptr = obj.load(std::memory_order_acquire);

        // #ifdef GARBAGE_BOUND_EXP
        //     if (tid == 1 || tid == 8 || tid == 12)
        //     {
        //         std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DELAY));
        //     }
        // #endif                

			uint64_t curr_epoch = getEpoch();
			if (curr_epoch == prev_epoch){
        // #ifdef GARBAGE_BOUND_EXP
        //     if (tid == 1 || tid == 8)
        //     {
        //         std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DELAY));
        //     }
        // #endif                
				return ptr;
			} else {
				// reservations[tid].ui[idx].store(curr_epoch, std::memory_order_release);
				reservations[tid].ui[idx].store(curr_epoch, std::memory_order_seq_cst);
				prev_epoch = curr_epoch;

        #ifdef GARBAGE_BOUND_EXP
            if (tid == 1)
            {
                // COUTATOMICTID("reserved e and sleep =" <<e <<std::endl);
                std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DELAY));
            }
        #endif                

			}
		}
    }

    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
        if (obj == NULL)
        {
            return;
        }
        uint64_t birth_epoch = obj->birth_epoch;
        std::list<HeInfo> *myTrash = &(retired[tid].ui);
        // for(auto it = myTrash->begin(); it!=myTrash->end(); it++){
        // 	assert(it->obj!=obj && "double retire error");
        // }

        uint64_t retire_epoch = getEpoch();
        myTrash->push_back(HeInfo(obj, birth_epoch, retire_epoch));
        retire_counters[tid] = retire_counters[tid] + 1;
        if (retire_counters[tid] % freq == 0)
        {
            empty(tid);
            // #ifdef GSTATS_HANDLE_STATS
            //         GSTATS_ADD(tid, num_signal_events, 1);
            // #endif        
        }

        if (retire_counters[tid] % 1000 == 0)
        {
        #ifdef USE_GSTATS
            GSTATS_APPEND(tid, reclamation_event_size, myTrash->size());
        #endif
        }
    }

    bool isFreeable(uint64_t birth_epoch, uint64_t retire_epoch)
    {
		// for (int i = 0; i < num_process; i++)
        // {
		// 	for (int j = 0; j < num_he; j++)
        //     {
		// 		// const uint64_t epo = reservations[i].ui[j].load(std::memory_order_acquire);

		// 		//NOTE: the third condition epo == 0 seems wrong in orig code.
        //         // what if T1 did 10 alloc. Thus, global epoch is still 0. And T2
        //         // decides to reclaim a obj that could be accessed in epoc 0 by some thread.
        //         // A crash would occur. Indeed enabling the check causes seg fault and commenting it gives non crashing execution.
        //         if (epo < birth_epoch || epo > retire_epoch || epo == 0)
        //         {
		// 			continue;
		// 		}
        //         else 
        //         {
		// 			return false;
		// 		}
		// 	}
		// }
		// return true;

        for (uint64_t ith = 0; ith < num_process*num_he; ith++)
        {
            const uint64_t epo = threadData[tid].scannedHEs[ith];

            if (epo < birth_epoch || epo > retire_epoch || epo == 0)
            {
                continue;
            }
            else 
            {
                return false;
            }
        }

		return true;
    }

    void empty(const int tid)
    {
        // erase safe objects
        std::list<HeInfo> *myTrash = &(retired[tid].ui);

        uint before_sz = myTrash->size();
        // COUTATOMICTID("decided to empty! bag size=" << myTrash->size() << std::endl);


        for (int i = 0; i < num_process; i++)
        {
			for (int j = 0; j < num_he; j++)
            {
                threadData[tid].scannedHEs[(i*num_he)+j] = reservations[i].ui[j].load(std::memory_order_acquire);
            }
        }

        for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;)
        {
            HeInfo res = *iterator;
    
            if (isFreeable(res.birth_epoch, res.retire_epoch))
            {
            // this->pool->add(tid, res.obj);
            #ifdef DEAMORTIZE_FREE_CALLS
                threadData[tid].deamortizedFreeables->add(res.obj);
            #else
                this->pool->add(tid, res.obj); //reclaim
            #endif

                iterator = myTrash->erase(iterator); //return iterator corresponding to next of last erased item
            }
            else
            {
                ++iterator;
            }
        }

        uint after_sz = myTrash->size();
        TRACE COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);
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

        threadData[tid].scannedHEs = new uint64_t [num_process * num_he];
    }
    void deinitThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif
        delete[] threadData[tid].scannedHEs;
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

    reclaimer_he(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_he helping=" << this->shouldHelp() << std::endl;
        num_process = numProcesses;
        // freq = 24576; //32768; //16384; //16384; //32000; //30;
#ifdef LONG_RUNNING_EXP
        freq = 2048; //32768; //16384; //32000; //30; //on lines with nbr
#else
        freq = 24576; //16384; //32000; //30; //on lines with nbr
#endif
        epoch_freq = 100;//150; // increasing this causes increase in mem usage
        num_he = 3; // 3 for hmlist 2 shoudl work for harris and lazylist

        retired = new padded<std::list<HeInfo>>[num_process];
        reservations = new padded< std::atomic<uint64_t>* >[num_process];
        for (int i = 0; i < num_process; i++)
        {
            reservations[i].ui = new std::atomic<uint64_t>[num_he];
            for (int j = 0; j < num_he; j++)
            {
                reservations[i].ui[j].store(0, std::memory_order_release);
            }
        }
        retire_counters = new padded<uint64_t>[num_process];
        alloc_counters = new padded<uint64_t>[num_process];
        epoch.store(1, std::memory_order_release);
    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif        
    }
    ~reclaimer_he()
    {
        // std::cout <<"reclaimer destructor started" <<std::endl;
        for (int i = 0; i < num_process; i++)
        {
            // COUTATOMIC(retired[i].ui.size()<<std::endl);
            for (auto iterator = retired[i].ui.begin(), end = retired[i].ui.end(); iterator != end; )
            {
                HeInfo res = *iterator;
                iterator=retired[i].ui.erase(iterator); //return iterator corresponding to next of last erased item
                this->pool->add(i, res.obj); //reclaim
            }

            delete [] reservations[i].ui;
        }

        delete [] retired;
        delete [] reservations;
        delete [] retire_counters;
        delete [] alloc_counters;
        // std::cout <<"reclaimer destructor finished" <<std::endl;
		COUTATOMIC("epoch_freq= " << epoch_freq << "empty_freq= " << freq <<std::endl);
    }
};

#endif //RECLAIM_HE_H
