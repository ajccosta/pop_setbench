/**
 * The code is adapted from https://github.com/urcs-sync/Interval-Based-Reclamation to make it work with Setbench.
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */


// Adding fast path RCU and fast POPHP reclamation makes EBR smrs robust but the applicability is limited by applicability of HPs. This applicability limitation can be relaxed using the recent HP++ technique.

#ifndef RECLAIM_RCU_POPHP_H
#define RECLAIM_RCU_POPHP_H

#include <list>
#include "ConcurrentPrimitives.h"
#include "blockbag.h"

// #if !defined HP_ORIGINAL_FREE || !HP_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif
#define NUM_POPHP 3
template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_rcu_pophp : public reclaimer_interface<T, Pool>
{
public:
    class RCUInfo
    {
    public:
        T *obj;
        uint64_t epoch;
        RCUInfo(T *obj, uint64_t epoch) : obj(obj), epoch(epoch) {}
    };


private:
    int num_process;
    int empty_freq;
    int epoch_freq; //RCU
    int slotsPerThread;
    // PAD;
    paddedAtomic<T*> *slots;
    // padded<int> *cntrs;
    // padded<std::list<T*>> *retired;
    // PAD;
    static const int MAX_RETIREBAG_CAPACITY_POW2 = 24576; //16384; //32768; //16384; //32768; //4096; //8192;//16384;//32768;          //16384;

    paddedAtomic<uint64_t> *reservations;
    padded<std::list<RCUInfo>> *retired;
    padded<uint64_t> *retire_counters;
    padded<uint64_t> *alloc_counters;
    std::atomic<uint64_t> epoch; //FIXME: pad?

    class ThreadData
    {
    private:
        PAD;
    public:
    #ifdef DEAMORTIZE_FREE_CALLS
        blockbag<T> * deamortizedFreeables;
        int numFreesPerStartOp;
    #endif

    unsigned int bagCapacityThreshold; // using this variable to set random thresholds for out of patience.
    int num_sigallattempts_since_last_attempt;
    T* local_slots[NUM_POPHP];

    //variables confirming publishing
    PAD;
    std::atomic<unsigned int> mypublishingTS; //SWMR slot to announce reservations published.
    PAD;
    unsigned int *myscannedTS; //saves the mypublishingTS of every other thread.

    ThreadData()
    {
    }
    private:
        PAD;
    };

    // PAD;
    ThreadData threadData[MAX_THREADS_POW2];
    PAD;

inline bool requestAllThreadsToRestart(const int tid)
{
    bool result = false;
#ifdef USE_GSTATS
    GSTATS_ADD(tid, signalall, 1);
#endif        
    for (int otherTid = 0; otherTid < this->NUM_PROCESSES; ++otherTid)
    {
        if (tid != otherTid)
        {
            //get posix thread id from application level thread id.
            pthread_t otherPthread = this->recoveryMgr->getPthread(otherTid);
            int error = 0;

            error = pthread_kill(otherPthread, this->recoveryMgr->neutralizeSignal);
            if (error)
            {
                COUTATOMICTID("Error when trying to pthread_kill(pthread_tFor(" << otherTid << "), " << this->recoveryMgr->neutralizeSignal << ")" << std::endl);
                if (error == ESRCH)
                    COUTATOMICTID("ESRCH" << std::endl);
                if (error == EINVAL)
                    COUTATOMICTID("EINVAL" << std::endl);

                assert("Error when trying to pthread_kill" && 0);
                return result;
            }
        }
    }     // for()



    result = true;
    return result;
}


public:

    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_rcu_pophp<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_rcu_pophp<_Tp1, _Tp2> other;
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

        uint64_t e = epoch.load(std::memory_order_acquire);
        reservations[tid].ui.store(e, std::memory_order_release/* ajreb std::memory_order_seq_cst */);
        #ifdef GARBAGE_BOUND_EXP
            if (num_process > 2 && (tid == 1 || tid == num_process - 1))
            {
                // COUTATOMICTID("reserved e and sleep =" <<e <<std::endl);
                std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DELAY));
            }
        #endif        
        return result;
    }

    inline void endOp(const int tid)
    {
        for(int i = 0; i<slotsPerThread; i++){
			// slots[tid*slotsPerThread+i] = NULL;
            // saving slotsPerThread global writes here
            threadData[tid].local_slots[i] = NULL;
		}
        reservations[tid].ui.store(UINT64_MAX, std::memory_order_release/*ajreb std::memory_order_seq_cst */);
    }

    inline void updateAllocCounterAndEpoch(const int tid)
    {
		alloc_counters[tid]=alloc_counters[tid]+1;
		if(alloc_counters[tid]%(epoch_freq*num_process)==0){
			epoch.fetch_add(1,std::memory_order_acq_rel);
		}
    }

    inline uint64_t getEpoch()
    {
        return epoch.load(std::memory_order_acquire);
    }

    /**Inner utility method for protect* idx is only used hazard era*/
    inline T* read(int tid, int idx, std::atomic<T*> &obj)
    {
		T* ret;
		T* realptr;
		while(true){
			ret = obj.load(std::memory_order_acquire);
			realptr = (T*)((size_t)ret & 0xfffffffffffffffc);
			reserve(realptr, idx, tid);
			if(ret == obj.load(std::memory_order_acquire)){
				return ret;
			}
		}
    }

    inline void reserve(T* ptr, int slot, int tid){
		// slots[tid*slotsPerThread+slot] = ptr;
        // saving per read global write here. But not saving anything here as other threads only read it during retire...
        threadData[tid].local_slots[slot] = ptr;
	}

    uint rcu_empty(const int tid)
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
        std::list<RCUInfo> *myTrash = &(retired[tid].ui);

        uint before_sz = myTrash->size();
        // COUTATOMICTID("decided to empty! bag size=" << myTrash->size() << std::endl);        

        for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;)
        {
            RCUInfo res = *iterator;
            if (res.epoch < minEpoch)
            {
                iterator = myTrash->erase(iterator); //return iterator corresponding to next of last erased item
                // this->pool->add(tid, res.obj);

                #ifdef DEAMORTIZE_FREE_CALLS
                    threadData[tid].deamortizedFreeables->add(res.obj);
                #else
                    this->pool->add(tid, res.obj); //reclaim
                #endif
            }
            else
            {
                ++iterator;
            }
        }
        uint after_sz = myTrash->size();
        TRACE COUTATOMICTID("After empty! bag size=" << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);
        return after_sz;
    }

    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
        if (obj == NULL)
        {
            return;
        }

        std::list<RCUInfo> *myTrash = &(retired[tid].ui);

        uint64_t e = epoch.load(std::memory_order_acquire);
        RCUInfo info = RCUInfo(obj, e);
        myTrash->push_back(info);
        retire_counters[tid] = retire_counters[tid] + 1;

        if (retire_counters[tid] % empty_freq == 0)
        {
            uint after_sz = rcu_empty(tid);
// #ifdef GSTATS_HANDLE_STATS
//             GSTATS_ADD(tid, num_signal_events, 1);
// #endif

            // If bag is still twice the size implies some thread is delayed and time to force reclamation using pop HPs.
            if (after_sz >= /* 2* */(empty_freq/3)) //was div 2
            {

                // scan all publishingTS to establish later that reservations were published after ping or signal
                for (int i = 0; i < num_process; i++){
                    threadData[tid].myscannedTS[i] = threadData[i].mypublishingTS.load(std::memory_order_acquire);
                }

                if (requestAllThreadsToRestart(tid))
                {
                    // confirm that all reservations were published after ping
                    int assert_count = 0;
                    for (int i = 0; i < num_process; i++){
                        if (tid != i)
                        {
                            if (threadData[tid].myscannedTS[i] == threadData[i].mypublishingTS.load(std::memory_order_acquire)) 
                            {
                                assert(++assert_count < 3*num_process && "sum thread's publishing not visible yet");
                                continue;
                            }
                        }
                    }

                    hp_empty(tid);
                }
            } //if(myTrash->size() >= 2*empty_freq) HP_empty

        } //rcu_empty

        if (retire_counters[tid] % 1000 == 0)
        {
        #ifdef USE_GSTATS
            GSTATS_APPEND(tid, reclamation_event_size, myTrash->size());
        #endif
        }

    }

    void hp_empty(const int tid)
    {
		std::list<RCUInfo>* myTrash = &(retired[tid].ui);
        uint before_sz = myTrash->size();       

		for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end; ) 
        {
			bool danger = false;
			
            RCUInfo res = *iterator;
            auto ptr = res.obj;
			for (int i = 0; i<num_process*slotsPerThread; i++){
				if(ptr == slots[i].ui){
					danger = true;
					break;
				}
			}
			if(!danger){
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
        // uint after_sz = myTrash->size();
        // TRACE COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);

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
        threadData[tid].bagCapacityThreshold = empty_freq;
        for (int j = 0; j < NUM_POPHP; j++)
        {
            threadData[tid].local_slots[j] = NULL;
        }
        threadData[tid].num_sigallattempts_since_last_attempt = 0;

        threadData[tid].mypublishingTS = 0;
        threadData[tid].myscannedTS = new unsigned int[num_process * PREFETCH_SIZE_WORDS];        
    }

    void deinitThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif
        delete[] threadData[tid].myscannedTS;
    }

    inline static bool isProtected(const int tid, T *const obj) { return true; }
    inline static bool isQProtected(const int tid, T *const obj) { return false; }
    inline static bool protect(const int tid, T *const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) { return true; }
    inline static void unprotect(const int tid, T *const obj) {}

    inline static bool qProtect(const int tid, T *const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) { return true; }
    inline static void qUnprotectAll(const int tid) {}
    inline static bool quiescenceIsPerRecordType() { return false; }

    // this helps in populating tid mapping for signal handling. This function is reused from the Debra+
    inline static bool needsSetJmp() { return true; }

    /**
     * To escape asserts at record manager assert(!supportcrash || isQuiescent())
    */
    inline static bool isQuiescent(const int tid)
    {
        return true;
    }

    // This function should be signal safe.
    // using printf/cout is not signal safe.
    inline void publishReservations(const int tid)
    {
        
        for(int i = 0; i<slotsPerThread; i++){
			slots[tid*slotsPerThread+i] = threadData[tid].local_slots[i];
		}
        // std::atomic_fetch_add(&threadData[tid].mypublishingTS, 1); //tell other threads that I commpleted publishing.        
        threadData[tid].mypublishingTS.fetch_add(1, std::memory_order_acq_rel);
        return;
    } 

    reclaimer_rcu_pophp(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_rcu_pophp helping=" << this->shouldHelp() << std::endl;
        num_process = numProcesses;
        empty_freq = 24576; //MAX_RETIREBAG_CAPACITY_POW2; //16384;//100; //30; // 32K gives best gains for AF version. larger or lower doesn't make a much difference.
        slotsPerThread = NUM_POPHP; //3;
        epoch_freq = 150; //150;   //this was default values ion IBR microbench

        slots = new paddedAtomic< T* >[num_process * slotsPerThread];
        for (int i = 0; i < num_process * slotsPerThread; i++)
        {
            slots[i] = NULL;
        }

        retired = new padded<std::list<RCUInfo>>[num_process];
        // cntrs = new padded<int>[num_process];
        reservations = new paddedAtomic<uint64_t>[numProcesses];
        retire_counters = new padded<uint64_t>[numProcesses];
        alloc_counters = new padded<uint64_t>[numProcesses];

        for (int i = 0; i < num_process; i++)
        {
            reservations[i].ui.store(UINT64_MAX, std::memory_order_release);
            retired[i].ui.clear();
        }
        epoch.store(0, std::memory_order_release);
    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif                
    }

    ~reclaimer_rcu_pophp()
    {
        for (int i = 0; i < num_process; i++)
        {
            // COUTATOMIC(retired[i].ui.size()<<std::endl);
            for (auto iterator = retired[i].ui.begin(), end = retired[i].ui.end(); iterator != end; )
            {
                RCUInfo res = *iterator;
                iterator=retired[i].ui.erase(iterator); //return iterator corresponding to next of last erased item
                this->pool->add(i, res.obj); //reclaim
            }

        }

        delete [] retired;
        delete [] reservations;
        delete [] retire_counters;
        delete [] alloc_counters;
        delete [] slots;

		COUTATOMIC("epoch_freq= " << epoch_freq << "empty_freq= " << empty_freq <<std::endl);
    }
};

#endif //RECLAIM_RCU_POPHP_H
