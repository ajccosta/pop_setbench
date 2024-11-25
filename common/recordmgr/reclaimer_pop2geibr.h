/**
 * The code is adapted from https://github.com/urcs-sync/Interval-Based-Reclamation to make it work with Setbench.
 * This file implements IBR memory reclamation (2geIBR variant mentioned in original Interval Based Memory Reclamation paper, PPOPP 2018).
 * The exact file for 2geibr is RangeTrackerNew.hpp in the IBR code.
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

#ifndef RECLAIM_POPIBR_H
#define RECLAIM_POPIBR_H

#include <list>
#include "ConcurrentPrimitives.h"
#include "blockbag.h"

// #if !defined IBR_ORIGINAL_FREE || !IBR_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_pop2geibr : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int freq;
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
        unsigned int bagCapacityThreshold; // using this variable to set random thresholds for out of patience.
        int num_sigallattempts_since_last_attempt;
        uint64_t local_upper_reserved_epoch; // lower era that is reserved but not published.
        uint64_t local_lower_reserved_epoch; // upper era that is reserved but not published.

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

    PAD;
    ThreadData threadData[MAX_THREADS_POW2];
    PAD;    


public:
    class IntervalInfo
    {
    public:
        T *obj;
        // birth epoch would come from ds node's field named birth epoch. 
        // this is to avoid doing a bad hack in allocator interface where I would need
        // to silently allocate a node larger than what DS implemntor defines to allocate memory for birth epoch.
        // It is better to declare birth epoch as part of DS implementor. Easy, straightforward and simple over tricky.
        uint64_t birth_epoch; 
        uint64_t retire_epoch;
        IntervalInfo(T *obj, uint64_t b_epoch, uint64_t r_epoch) : obj(obj), birth_epoch(b_epoch), retire_epoch(r_epoch) {}
    };

private:
    paddedAtomic<uint64_t> *upper_reservs;
    paddedAtomic<uint64_t> *lower_reservs;
    // padded<uint64_t> *retire_counters;
    padded<uint64_t> *alloc_counters;
    padded<std::list<IntervalInfo>> *retired;

    std::atomic<uint64_t> epoch;
    PAD;

    inline bool requestAllThreadsToRestart(const int tid)
    {
        bool result = false;

        for (int otherTid = 0; otherTid < this->NUM_PROCESSES; ++otherTid)
        {
            if (tid != otherTid)
            {
                //get posix thread id from application level thread id.
                pthread_t otherPthread = this->recoveryMgr->getPthread(otherTid);
                int error = 0;
                //send signal to other thread
                // COUTATOMICTID("DEBUG_TID_MAP::"
                //                     << " tid=" << tid << " with pid=" << pthread_self() << " registeredThreads[" << tid << "]=" << registeredThreads[tid] << " sending sig to tid= " << otherTid << " with pid=" << otherPthread << " registeredThreads[" << otherTid << "]=" << registeredThreads[otherTid] << std::endl);

                // FIXME: I Don't know, should signal be UNblocked after a thread executes a sigandler. So that it could receive sig again?
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
                // else
                // {
                //     COUTATOMICTID(" Signal sent via pthread_kill(pthread_tFor(" << otherTid << "  " << otherPthread << "), " << this->recoveryMgr->neutralizeSignal << ")" << std::endl);
                // }
            }
            // else
            // {
            //     // publish own reservations
            //     publishReservations(tid);
            // } // if (tid != otherTid){
        }     // for()

    #ifdef USE_GSTATS
        GSTATS_ADD(tid, signalall, 1);
    #endif        

        result = true;
        return result;
    }

public:
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_pop2geibr<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_pop2geibr<_Tp1, _Tp2> other;
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

        // instead of writing reservations globally store them locally unless asked
        threadData[tid].local_lower_reserved_epoch = e;
        threadData[tid].local_upper_reserved_epoch = e;
        // saved two seqcst stores per start operation

        // lower_reservs[tid].ui.store(e, std::memory_order_seq_cst);
        // upper_reservs[tid].ui.store(e, std::memory_order_seq_cst);

        return result;
    }

    inline void endOp(const int tid)
    {
        // upper_reservs[tid].ui.store(UINT64_MAX, std::memory_order_release);
        // lower_reservs[tid].ui.store(UINT64_MAX, std::memory_order_release);
        
        // saving two seqcst stores per end operation. Total 4 stores per operation.
        threadData[tid].local_lower_reserved_epoch = UINT64_MAX;
        threadData[tid].local_upper_reserved_epoch = UINT64_MAX;
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
    T* read(int tid, int idx, std::atomic<T*> &obj)
    {
        // uint64_t prev_epoch = upper_reservs[tid].ui.load(std::memory_order_acquire);
        uint64_t prev_epoch = threadData[tid].local_upper_reserved_epoch; // saving one global load per read operation

        while (true)
        {
            // assert(obj && "obj null");
            T* ptr = obj.load(std::memory_order_acquire);
        
            uint64_t curr_epoch = getEpoch();
            if (curr_epoch == prev_epoch)
            {
                return ptr; //fast path to avoid a store if epoch hasnt changed
            }
            else
            {
                // upper_reservs[tid].ui.store(curr_epoch, std::memory_order_seq_cst);
                // saving atleast one seqcst store per read operation
                threadData[tid].local_upper_reserved_epoch = curr_epoch;
                prev_epoch = curr_epoch;
            }
        }
    }
    // T* read(int tid, std::atomic<T*> &obj)
    // {
    //     return nullptr;
    // }

    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
        if (obj == NULL)
        {
            return;
        }
        uint64_t birth_epoch = obj->birth_epoch;
        std::list<IntervalInfo> *myTrash = &(retired[tid].ui);
        // for(auto it = myTrash->begin(); it!=myTrash->end(); it++){
        // 	assert(it->obj!=obj && "double retire error");
        // }

        uint64_t retire_epoch = getEpoch();
        myTrash->push_back(IntervalInfo(obj, birth_epoch, retire_epoch));
        size_t myTrashSize = myTrash->size();
        // retire_counters[tid] = retire_counters[tid] + 1;
        // if (retire_counters[tid] % freq == 0)

        if (((++threadData[tid].num_sigallattempts_since_last_attempt) > (threadData[tid].bagCapacityThreshold/2) ) && myTrashSize >= threadData[tid].bagCapacityThreshold)
        {
            for (int i = 0; i < num_process; i++){
                threadData[tid].myscannedTS[i] = threadData[i].mypublishingTS.load(std::memory_order_acquire);
            }

            if (requestAllThreadsToRestart(tid))
            {
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

                empty(tid);
                #ifdef USE_GSTATS
                    GSTATS_APPEND(tid, reclamation_event_size, myTrashSize);
                #endif
                //reset num_sigallattempts_since_last_attempt
                threadData[tid].num_sigallattempts_since_last_attempt = 0;                
            }        
        }
    }

    bool conflict(uint64_t *lower_epochs, uint64_t *upper_epochs, uint64_t birth_epoch, uint64_t retire_epoch)
    {
        for (int i = 0; i < num_process; i++)
        {
            if (upper_epochs[i] >= birth_epoch && lower_epochs[i] <= retire_epoch)
            {
                return true;
            }
        }
        return false;
    }

    void empty(const int tid)
    {
        //read all epochs
        uint64_t upper_epochs_arr[num_process];
        uint64_t lower_epochs_arr[num_process];
        for (int i = 0; i < num_process; i++)
        {
            //sequence matters.
            lower_epochs_arr[i] = lower_reservs[i].ui.load(std::memory_order_acquire);
            upper_epochs_arr[i] = upper_reservs[i].ui.load(std::memory_order_acquire);
        }

        // erase safe objects
        std::list<IntervalInfo> *myTrash = &(retired[tid].ui);

        uint before_sz = myTrash->size();
        #ifdef USE_GSTATS
            GSTATS_APPEND(tid, reclamation_event_size, before_sz);
        #endif        
        // COUTATOMICTID("decided to empty! bag size=" << myTrash->size() << std::endl);

        for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;)
        {
            IntervalInfo res = *iterator;
            if (!conflict(lower_epochs_arr, upper_epochs_arr, res.birth_epoch, res.retire_epoch))
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
        // COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);
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
        threadData[tid].bagCapacityThreshold = freq;
        threadData[tid].num_sigallattempts_since_last_attempt = 0;
        threadData[tid].local_upper_reserved_epoch = UINT64_MAX;
        threadData[tid].local_lower_reserved_epoch = UINT64_MAX;

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

    inline static bool needsSetJmp() { return true; }
    /**
     * To escape asserts at record manager assert(!supportcrash || isQuiescent())
    */
    inline static bool isQuiescent(const int tid)
    {
        return true;
    }


    inline void publishReservations(const int tid)
    {
        // FIXME: may be cst mem order?
        // upper_reservs[tid].ui.store(threadData[tid].local_upper_reserved_epoch, std::memory_order_release);
        // lower_reservs[tid].ui.store(threadData[tid].local_lower_reserved_epoch, std::memory_order_release);

        upper_reservs[tid].ui.store(threadData[tid].local_upper_reserved_epoch, std::memory_order_seq_cst);
        lower_reservs[tid].ui.store(threadData[tid].local_lower_reserved_epoch, std::memory_order_seq_cst);

        threadData[tid].mypublishingTS.fetch_add(1, std::memory_order_acq_rel);

        return;
    }    

    reclaimer_pop2geibr(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_pop2geibr helping=" << this->shouldHelp() << std::endl;
        num_process = numProcesses;
        freq = 32768; //16384; //32000; //30; //on lines with nbr
        epoch_freq = 50; // if high then high mem usage

        retired = new padded<std::list<IntervalInfo>>[num_process];
        upper_reservs = new paddedAtomic<uint64_t>[num_process];
        lower_reservs = new paddedAtomic<uint64_t>[num_process];
        for (int i = 0; i < num_process; i++)
        {
            upper_reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
            lower_reservs[i].ui.store(UINT64_MAX, std::memory_order_release);
        }
        // retire_counters = new padded<uint64_t>[num_process];
        alloc_counters = new padded<uint64_t>[num_process];
        epoch.store(0, std::memory_order_release);
    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif                
    }
    ~reclaimer_pop2geibr()
    {

        for (int i = 0; i < num_process; i++)
        {
            // COUTATOMIC(retired[i].ui.size()<<std::endl);
            for (auto iterator = retired[i].ui.begin(), end = retired[i].ui.end(); iterator != end; )
            {
                IntervalInfo res = *iterator;
                iterator=retired[i].ui.erase(iterator); //return iterator corresponding to next of last erased item
                this->pool->add(i, res.obj); //reclaim
            }

        }

        delete [] retired;
        delete [] upper_reservs;
        delete [] lower_reservs;
        // delete [] retire_counters;
        delete [] alloc_counters;
		COUTATOMIC("epoch_freq= " << epoch_freq << "empty_freq= " << freq <<std::endl);
    }
};

#endif //RECLAIM_POPIBR_H
