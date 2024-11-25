/**
 * The code is adapted from https://github.com/urcs-sync/Interval-Based-Reclamation to make it work with Setbench.
 * This file implements HE memory reclamation (in original Interval Based Memory Reclamation paper, PPOPP 2018).
 * The exact file for 2geibr is RangeTrackerNew.hpp in the IBR code.
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

#ifndef RECLAIM_POPPLUSHP_H
#define RECLAIM_POPPLUSHP_H

#include <list>
#include "ConcurrentPrimitives.h"
#include "blockbag.h"

// #if !defined HP_ORIGINAL_FREE || !HP_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif
#define NUM_POPHP 3
template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_ibr_popplushp : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int empty_freq;
    int slotsPerThread;
    // PAD;
    paddedAtomic<T*> *slots;
    padded<uint64_t> *cntrs;
    padded<std::list<T*>> *retired;
    // paddedAtomic<uint64_t> publishing_epoch;
    // PAD;
    // padded<uint64_t> *retire_counters;

    static const int MAX_RETIREBAG_CAPACITY_POW2 = 24576; //16384; //32768; //16384; //32768; //4096; //8192;//16384;//32768;          //16384;

    class ThreadData
    {
    private:
        PAD;
    public:
    #ifdef DEAMORTIZE_FREE_CALLS
        blockbag<T> * deamortizedFreeables;
        int numFreesPerStartOp;
    #endif

    unsigned int bagCapacityThreshold; // NOT used, we use cntr and retire_freq instead.

    //BEGIN OPTIMIZED_SIGNAL: LoWatermark variables
    PAD;
    std::atomic<unsigned int> announcedTS; //SWMR slot announce ts before and after sending signals, assuming won't overflow.
    PAD;
    unsigned int *savedTS; //saves the announcedTS of every other thread

    //variables confirming publishing
    PAD;
    std::atomic<unsigned int> mypublishingTS; //SWMR slot to announce reservations published.
    PAD;
    unsigned int *myscannedTS; //saves the mypublishingTS of every other thread.

    T* local_slots[NUM_POPHP];

    // vars to learn sigAll events occured and reservations were published by all threads so that a thread could reclaim at loWm path.
    // uint64_t saved_publishing_epoch; // at loWm saves the last global TS observed, set by thread at HiWm

    bool firstLoEntryFlag;                   //= true;
    unsigned int numRetiresSinceLoWatermark; // = 0;
    unsigned int LoPathReclaimAttemptFreq;

    unsigned int retire_bag_size_when_entered_loWm; // = 0;

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
    
    //Theorem: The signalling thread should also publish it's epochs so that other threads in LoWm could reclaim correctly.
    publishReservations(tid);

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


    // publishing_epoch.ui.fetch_add(1);
    // threadData[tid].saved_publishing_epoch = publishing_epoch.ui.load(std::memory_order_acquire);

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
        typedef reclaimer_ibr_popplushp<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_ibr_popplushp<_Tp1, _Tp2> other;
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
			// slots[tid*slotsPerThread+i] = NULL;
            // saving slotsPerThread global writes here
            threadData[tid].local_slots[i] = NULL;
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


    inline void setLoWmMetaData(const int tid)
    {
        threadData[tid].firstLoEntryFlag = false;

        // approach3: 1, 5, 10, 15....50%
        int rand_val = (rand()%11);
        threadData[tid].LoPathReclaimAttemptFreq = (rand_val==0)? (0.01*MAX_RETIREBAG_CAPACITY_POW2) : (unsigned int)((((rand_val*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);

        // threadData[tid].LoPathReclaimAttemptFreq = 50; //30;

        //take relaxed snapshot of all other announceTS, To be used to know if its time to reclaim at lowpTh. 
        for (int i = 0; i < num_process; i++){
            threadData[tid].savedTS[i] = threadData[i].announcedTS;
        }        
    }

    inline void resetLoWmMetaData(const int tid)
    {
        threadData[tid].firstLoEntryFlag = true;
        threadData[tid].numRetiresSinceLoWatermark=0;
        threadData[tid].retire_bag_size_when_entered_loWm = 0;
    }

    inline bool isPastLoWatermark(const int tid, size_t myTrashSize = 0)
    {

        return (
            (myTrashSize > threadData[tid].bagCapacityThreshold/2) 
            && 
            ((threadData[tid].numRetiresSinceLoWatermark % threadData[tid].LoPathReclaimAttemptFreq) == 0)
            );
    }

    inline bool isOutOfPatience(const int tid, size_t myTrashSize = 0)
    {
        bool result = (myTrashSize >= (empty_freq));
        // bool result = (myTrashSize >= (num_process > 192 ? 2*empty_freq : empty_freq));
        // if (result) COUTATOMICTID("myTrashSize="<<myTrashSize<< " empty_freq =" << empty_freq<< " cntrs["<<tid<<"]="<<cntrs[tid]<<std::endl);
        return result;
    }

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

    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
        if (obj == NULL)
        {
            return;
        }
        std::list<T*> *myTrash = &(retired[tid].ui);
        myTrash->push_back(obj);
        size_t myTrashSize = myTrash->size();

		if(isOutOfPatience(tid, myTrashSize)){
            // #ifdef GSTATS_HANDLE_STATS
            //         GSTATS_ADD(tid, num_signal_events, 1);
            // #endif        
            resetLoWmMetaData(tid);
			// cntrs[tid] = 0;

            for (int i = 0; i < num_process; i++){
                threadData[tid].myscannedTS[i] = threadData[i].mypublishingTS.load(std::memory_order_acquire);
            }            
            std::atomic_fetch_add(&threadData[tid].announcedTS, 1); //tell other threads that I am starting signalling.
            if (requestAllThreadsToRestart(tid))
            {
                #ifdef POP_DEBUG                
                for (int i = 0; i < num_process; i++)
                {
                    for (int j = 0; j < slotsPerThread; j++)
                    {
                        COUTATOMICTID(slots[i*slotsPerThread+j] << ":" );
                    }
                    COUTATOMIC(std::endl);
                }
                #endif
                std::atomic_fetch_add(&threadData[tid].announcedTS, 1); //tell other threads that I am done signalling

                // ensure all threads published.
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
                // COUTATOMICTID("reclaimed at HiWm=" <<myTrashSize <<" aftersize=" << myTrash->size() << std::endl);
            }
		}
        else if(isPastLoWatermark(tid, myTrashSize))
        {
            if(threadData[tid].firstLoEntryFlag){
                setLoWmMetaData(tid);
                threadData[tid].retire_bag_size_when_entered_loWm = myTrashSize;
                // threadData[tid].saved_publishing_epoch = publishing_epoch.ui.load(std::memory_order_acquire);
            }

            // check if new signal was sent
            // int64_t new_publishing_epoch = publishing_epoch.ui.load(std::memory_order_acquire);
            // if (threadData[tid].saved_publishing_epoch != new_publishing_epoch)
            for (int i = 0; i < num_process; i++)
            {   //TODO: skip self comparison.
                if( threadData[i].announcedTS >= threadData[tid].savedTS[i] + 2)
                {
                    // threadData[tid].saved_publishing_epoch = new_publishing_epoch;

                    //reclaim
                    // #ifdef USE_GSTATS
                    //     GSTATS_APPEND(tid, lo_reclamation_event_size, myTrashSize);
                    // #endif
                    empty(tid);

                    resetLoWmMetaData(tid);
                    break;
                    // thread exits LoWm path
                }
            }
        }

        // start counting num retires since a thread entered LoWm path
        if(!threadData[tid].firstLoEntryFlag)
            threadData[tid].numRetiresSinceLoWatermark++;        

		cntrs[tid].ui++;
        if(cntrs[tid].ui% (1000) == 0){
        #ifdef USE_GSTATS
            GSTATS_APPEND(tid, reclamation_event_size, myTrash->size());
        #endif            
        }
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

        T* temp_local_slots[num_process*slotsPerThread];
        
        for (int i = 0; i<num_process*slotsPerThread; i++){
            temp_local_slots[i] = slots[i].ui;
        }        

        int reclaimed_so_far = 0;
		for (typename std::list<T*>::iterator iterator = myTrash->begin(), end = myTrash->end(); iterator != end; ) 
        {

            // If reclaiming at loWm, only reclaim upto the point when entered loWm.
            // NOTE: ensure when calld from HiWm path this doesnt execute as ity will prevent reclaiming safe objects at HiWm.
            if ((!threadData[tid].firstLoEntryFlag) && (reclaimed_so_far > (threadData[tid].retire_bag_size_when_entered_loWm - 1)))
            {
                break;
            }
            reclaimed_so_far++;

			bool danger = false;
			auto ptr = *iterator;
            if (conflict(temp_local_slots, ptr))
            {
                danger = true;
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
        // COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);

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
        // threadData[tid].saved_publishing_epoch = 0;
        threadData[tid].firstLoEntryFlag = true;
        threadData[tid].retire_bag_size_when_entered_loWm = 0;
        threadData[tid].numRetiresSinceLoWatermark = 0;
        threadData[tid].LoPathReclaimAttemptFreq = MAX_RETIREBAG_CAPACITY_POW2/3; //default 1/2 of Max bagsize // FIXME: I hardcoded this value later in setLoWmMetaData.  

        threadData[tid].announcedTS = 0;
        threadData[tid].savedTS = new unsigned int[num_process * PREFETCH_SIZE_WORDS]; 

        threadData[tid].mypublishingTS = 0;
        threadData[tid].myscannedTS = new unsigned int[num_process * PREFETCH_SIZE_WORDS];                
    }

    void deinitThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif

        delete[] threadData[tid].savedTS;
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
        threadData[tid].mypublishingTS.fetch_add(1, std::memory_order_acq_rel);              
        return;
    } 

    reclaimer_ibr_popplushp(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_ibr_popplushp helping=" << this->shouldHelp() << std::endl;
        num_process = numProcesses;
        // empty_freq = MAX_RETIREBAG_CAPACITY_POW2; //16384;//100; //30; // 32K gives best gains for AF version. larger or lower doesn't make a much difference.
#ifdef LONG_RUNNING_EXP
        empty_freq = 2048; //32768; //16384; //32000; //30; //on lines with nbr
#else
        empty_freq = MAX_RETIREBAG_CAPACITY_POW2; //16384; //32000; //30; //on lines with nbr
#endif

        slotsPerThread = NUM_POPHP; //3;

        slots = new paddedAtomic< T* >[num_process * slotsPerThread];
        for (int i = 0; i < num_process * slotsPerThread; i++)
        {
            slots[i] = NULL;
        }

        retired = new padded<std::list<T*>>[num_process];
        cntrs = new padded<uint64_t>[num_process];

        for (int i = 0; i < num_process; i++)
        {
            cntrs[i] = 0;
            retired[i].ui = std::list<T*> ();
        }
        // publishing_epoch.ui.store(0);

    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif                
        // srand(time(NULL));
    }

    ~reclaimer_ibr_popplushp()
    {
        delete [] retired;
        delete [] slots;
		COUTATOMIC("empty_freq= " << empty_freq <<std::endl);
    }
};

#endif //RECLAIM_POPPLUSHP_H
