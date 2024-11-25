/**
 * The file implements NBR with POP technique. Helps to increase applicability and improve usability at cost of per read overhead. to use POP with NBR and be able to get rid of restarting and read/write phase op structure need to use read ops that reserve epochs like in HE.  So POP + HE == usable and widely applicable NBR. Also NBR_POP only published the reservation swhen signalled so per read overhead of HE is reduced.

 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

// TODO: remove birthepoch from HeInfo and DS.

#ifndef RECLAIM_NBR_POPPLUSHE_H
#define RECLAIM_NBR_POPPLUSHE_H

#include <list>
#include "ConcurrentPrimitives.h"
#include "blockbag.h"

// #if !defined HE_ORIGINAL_FREE || !HE_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif
#define NUM_POPHE 3

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_nbr_popplushe : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int freq;
    int epoch_freq;
    // int num_he;
    // PAD; 
    
    static const int MAX_RETIREBAG_CAPACITY_POW2 = 24576; //16384; //32768; //16384; //32768; //4096; //8192;//16384;//32768;          //16384;
    static const int NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK = 1024; //512; //

    class ThreadData
    {
    private:
        PAD;
    public:
    #ifdef DEAMORTIZE_FREE_CALLS
        blockbag<T> * deamortizedFreeables;
    #endif    
        unsigned int bagCapacityThreshold; // using this variable to set random thresholds for out of patience.
        int retires_since_HiWm;
        uint64_t local_reserved_epoch[NUM_POPHE]; // pointers that are reserved but not published.

        // vars to learn sigAll events occured and reservations were published by all threads so that a thread could reclaim at loWm path.
        uint64_t saved_publishing_epoch; // at loWm saves the last global TS observed, set by thread at HiWm

        bool firstLoEntryFlag;                   //= true;
        unsigned int numRetiresSinceLoWatermark; // = 0;
        unsigned int LoPathReclaimAttemptFreq;

        unsigned int retire_bag_size_when_entered_loWm; // = 0;

        // to save global reservations once before emptying retired bag.
        uint64_t *scannedHEs;

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

    // FIXME: following vars could be of padded type, instead of using a separate PAD.
    PAD;
    std::atomic<uint64_t> publishing_epoch; 
    PAD;    
    std::atomic<uint64_t> epoch; 
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
            } // if (tid != otherTid){
        }     // for()

        // increment the publishing epoch so that LoWm threads could reclaim.
        //FIXME: using a global atomic var seems slow? Shoudl use lamport clock like original NBR+?


        publishing_epoch.fetch_add(1);
        threadData[tid].saved_publishing_epoch = publishing_epoch.load(std::memory_order_acquire);

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
        typedef reclaimer_nbr_popplushe<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_nbr_popplushe<_Tp1, _Tp2> other;
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
        for (int i = 0; i < NUM_POPHE; i++)
        {
			// reservations[tid].ui[i].store(0, std::memory_order_seq_cst);
            threadData[tid].local_reserved_epoch[i] = 0;
		}        
    }

    inline void updateAllocCounterAndEpoch(const int tid)
    {
		alloc_counters[tid]=alloc_counters[tid]+1;
		if(alloc_counters[tid]%(epoch_freq*num_process)==0){
			epoch.fetch_add(1, std::memory_order_acq_rel);
		}
    }

    inline uint64_t getEpoch()
    {
        return epoch.load(std::memory_order_acquire);
    }

    inline void setLoWmMetaData(const int tid)
    {
        threadData[tid].firstLoEntryFlag = false;

        // approach3: 1, 5, 10, 15....50%
        // setLoWmTryReclaimFrequency();
        int rand_val = (rand()%11);
        threadData[tid].LoPathReclaimAttemptFreq = (rand_val==0)? (0.1*MAX_RETIREBAG_CAPACITY_POW2) : (unsigned int)((((rand_val*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);

        // threadData[tid].LoPathReclaimAttemptFreq = 90; //30;
    }

    inline void resetLoWmMetaData(const int tid)
    {
        threadData[tid].firstLoEntryFlag = true;
        threadData[tid].numRetiresSinceLoWatermark=0;
    }

    inline bool isPastLoWatermark(const int tid, size_t myTrashSize = 0)
    {

        return (
            (myTrashSize > threadData[tid].bagCapacityThreshold/4) 
            && 
            ((threadData[tid].numRetiresSinceLoWatermark % threadData[tid].LoPathReclaimAttemptFreq) == 0)
            );
    }

    inline bool isOutOfPatience(const int tid, size_t myTrashSize = 0)
    {
        return (
            (myTrashSize >= threadData[tid].bagCapacityThreshold)
            &&
            ((threadData[tid].retires_since_HiWm)%512 == 0)
            );
    }

    inline T* read(int tid, int idx, std::atomic<T*> &obj)
    {
        #ifdef GARBAGE_BOUND_EXP
            if (tid == 1)
            {
                // COUTATOMICTID("reserved e and sleep =" <<e <<std::endl);
                std::this_thread::sleep_for(std::chrono::seconds(SLEEP_DELAY));
            }
        #endif                        
		uint64_t prev_epoch = threadData[tid].local_reserved_epoch[idx];
		while(true){
            //FIXME: Prove memory order fence is needed.
			T* ptr = obj.load(std::memory_order_acquire); 
			uint64_t curr_epoch = getEpoch();
			if (curr_epoch == prev_epoch){
				return ptr;
			}
            // skip publishing untill signalled. Save locally so that it can be published when pinged.
            threadData[tid].local_reserved_epoch[idx] = curr_epoch;
            prev_epoch = curr_epoch;


		}
    }


    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
        assert(obj && "object to be retired in NULL");

        std::list<HeInfo> *myTrash = &(retired[tid].ui);
        uint64_t birth_epoch = obj->birth_epoch;
        uint64_t retire_epoch = getEpoch();
        myTrash->push_back(HeInfo(obj, birth_epoch, retire_epoch));
        size_t myTrashSize = myTrash->size();


        // NOTE: unlike NBR, in POP+HE Hi Wm doesn't guarantee that all objects in the bag will be reclaimed. If all object's be and re conflict with epochs (see is Freeable) then the bag may still be full even after sigAll at HiWm. Robustness or bound on unreclaimed records is similar to HE.
        if (isOutOfPatience(tid, myTrashSize))
        {
            resetLoWmMetaData(tid);
            threadData[tid].retire_bag_size_when_entered_loWm = 0;


            // if bagthreshold has been crossed then ping all threads to publish there reservations. If 0 or a very few records were reclaimed then bag will get full earlier the next time and signal overhead will be incurred. To avoid this ie the frequent signalling overhead we should only attenmpt signalling once atleast half of max size more have been retired ad hope that this time published reserved epochs let us reclaim mor objects. This trades high mem consumption with low sigoverhead. 
            // sigall all threads so that they can publish their reserved epochs.
            for (int i = 0; i < num_process; i++){
                threadData[tid].myscannedTS[i] = threadData[i].mypublishingTS.load(std::memory_order_acquire);
            }

            if (requestAllThreadsToRestart(tid))
            {

                // ensure all threads published.
        // #ifndef GARBAGE_BOUND_EXP
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
        // #endif

                // After being signalled all threads should have announced there reserved epochs 
                // This reclaimer will use those reservations to identify unsafe objects.
                // print reserved epoch here
                bool empty_result = empty(tid);
                // #ifdef USE_GSTATS
                //     GSTATS_APPEND(tid, reclamation_event_size, myTrashSize);
                // #endif
                //reset retires_since_HiWm
                if (empty_result) 
                {
                    threadData[tid].retires_since_HiWm = 0;
                }
                else 
                {
                    ++threadData[tid].retires_since_HiWm;
                }
            }
        }
        else if (isPastLoWatermark(tid, myTrashSize))
        {
            if(threadData[tid].firstLoEntryFlag){
                setLoWmMetaData(tid);
                threadData[tid].retire_bag_size_when_entered_loWm = myTrashSize;
                threadData[tid].saved_publishing_epoch = publishing_epoch.load(std::memory_order_acquire);
            }

            // check if new signal was sent
            int64_t new_publishing_epoch = publishing_epoch.load(std::memory_order_acquire);
            if (threadData[tid].saved_publishing_epoch != new_publishing_epoch)
            {
                threadData[tid].saved_publishing_epoch = new_publishing_epoch;

                //reclaim
                // #ifdef USE_GSTATS
                //     GSTATS_APPEND(tid, lo_reclamation_event_size, myTrashSize);
                // #endif
                empty(tid);

                resetLoWmMetaData(tid);
                threadData[tid].retire_bag_size_when_entered_loWm = 0;

                // thread exits LoWm path
            }
        }
        // start counting num retires since a thread entered LoWm path
        if(!threadData[tid].firstLoEntryFlag)
            threadData[tid].numRetiresSinceLoWatermark++;   

        
        retire_counters[tid] = retire_counters[tid] + 1;
        if (retire_counters[tid] % 1000 == 0)
        {
        #ifdef USE_GSTATS
            GSTATS_APPEND(tid, reclamation_event_size, myTrash->size());
        #endif
        }             
    }


    inline bool isFreeable(uint64_t birth_epoch, uint64_t retire_epoch, const int tid)
    {
		// for (int i = 0; i < num_process; i++)
        // {
		// 	for (int j = 0; j < NUM_POPHE; j++)
        //     {
		// 		const uint64_t epo = reservations[i].ui[j].load(std::memory_order_acquire); // FIXME: incurring a cost due to strict memory order for each object scanned is too many times... optimize this.

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

        for (uint64_t ith = 0; ith < num_process*NUM_POPHE; ith++)
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

    // return type of empty() is bool. This is only specific for POPPLUS.
    bool empty(const int tid)
    {
        
        // erase safe objects
        std::list<HeInfo> *myTrash = &(retired[tid].ui);

        uint before_sz = myTrash->size();
        // #ifdef USE_GSTATS
        //     GSTATS_APPEND(tid, reclamation_event_size, before_sz);
        // #endif        
        // if (0 == tid) COUTATOMICTID("decided to empty! bag size=" << myTrash->size() << " min_reserved_epoch=" << min_reserved_epoch <<std::endl);

        for (int i = 0; i < num_process; i++)
        {
			for (int j = 0; j < NUM_POPHE; j++)
            {
                threadData[tid].scannedHEs[(i*NUM_POPHE)+j] = reservations[i].ui[j].load(std::memory_order_acquire);
            }
        }

        // int delme_num_reclaimed = 0, delme_cntr = 0;
        int reclaimed_so_far = 0;
        for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;)
        {
            // only reclaimed upto the point when entered loWm.
            if ((!threadData[tid].firstLoEntryFlag) && (reclaimed_so_far > (threadData[tid].retire_bag_size_when_entered_loWm - 1)))
            {
                break;
            }
            reclaimed_so_far++;

            HeInfo res = *iterator;
            
            // if (0 == tid && (++delme_cntr < 10 || delme_cntr > before_sz-10)) COUTATOMIC("re="<<res.retire_epoch<<" ");

            if (isFreeable(res.birth_epoch, res.retire_epoch, tid))
            {
            // this->pool->add(tid, res.obj);
            #ifdef DEAMORTIZE_FREE_CALLS
                threadData[tid].deamortizedFreeables->add(res.obj);
            #else
                this->pool->add(tid, res.obj); //reclaim
                // ++delme_num_reclaimed;
            #endif

                iterator = myTrash->erase(iterator); //return iterator corresponding to next of last erased item
            }
            else
            {
                // OPTIMIZATION2
                // I can break frm the loop. as all the following objects are guaranteed to have a retire epoch greater than min_reserved_epoch. 
                // FIXME: Proof needed.
                ++iterator;
            }
        }

#ifdef POP_DEBUG 
        uint after_sz = myTrash->size();
        COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) /* << " delme_num_reclaimed=" << delme_num_reclaimed */ << std::endl);
#endif

        uint after_sz = myTrash->size();
        // if (0 == tid) COUTATOMICTID("\nbefore_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << " delme_num_reclaimed=" << delme_num_reclaimed << std::endl);

        return !(after_sz >= (threadData[tid].bagCapacityThreshold)); // if bag is still full, return false. Used later to amortize sigAll at HiWm path.

    }

    void debugPrintStatus(const int tid)
    {
    }

    void initThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = new blockbag<T>(tid, this->pool->blockpools[tid]);
        // threadData[tid].numFreesPerStartOp = 1;
#endif

        // if (num_process > 1)
        // {
        //     int num = rand()%100;
        //     if (num < 5)
        //         threadData[tid].bagCapacityThreshold = freq/8;
        //     else if (num < 20)
        //         threadData[tid].bagCapacityThreshold = freq/4;
        //     else if (num < 95)
        //         threadData[tid].bagCapacityThreshold = freq/2;
        //     else
        //         threadData[tid].bagCapacityThreshold = freq;
        // }
        // else
        {
            threadData[tid].bagCapacityThreshold = MAX_RETIREBAG_CAPACITY_POW2;
        }

        for (int j = 0; j < NUM_POPHE; j++)
        {
            threadData[tid].local_reserved_epoch[j] = 0;
        }

        threadData[tid].scannedHEs = new uint64_t [num_process * NUM_POPHE];

        threadData[tid].retires_since_HiWm = 0;
        threadData[tid].saved_publishing_epoch = 0;
        threadData[tid].firstLoEntryFlag = true;
        threadData[tid].retire_bag_size_when_entered_loWm = 0;
        threadData[tid].numRetiresSinceLoWatermark = 0;
        threadData[tid].LoPathReclaimAttemptFreq = MAX_RETIREBAG_CAPACITY_POW2/3; //default 1/2 of Max bagsize // FIXME: I hardcoded this value later in setLoWmMetaData.
        threadData[tid].mypublishingTS = 0;
        threadData[tid].myscannedTS = new unsigned int[num_process * PREFETCH_SIZE_WORDS];                       
    }
    void deinitThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif
        // COUTATOMICTID("bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold << std::endl);
        delete[] threadData[tid].scannedHEs;
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
        
        for (int j = 0; j < NUM_POPHE; j++)
        {
            reservations[tid].ui[j].store(threadData[tid].local_reserved_epoch[j], std::memory_order_seq_cst); 
            // FIXME: should it be relaxed? I think no, we need to ensure that subsequent loads do not get redordered.
        }
        threadData[tid].mypublishingTS.fetch_add(1, std::memory_order_acq_rel);              
        return;
    }    

    reclaimer_nbr_popplushe(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_nbr_popplushe helping=" << this->shouldHelp() << std::endl;
        num_process = numProcesses;
        // freq = MAX_RETIREBAG_CAPACITY_POW2; //16384; //32768; //30;
#ifdef LONG_RUNNING_EXP
        freq = 2048; //32768; //16384; //32000; //30; //on lines with nbr
#else
        freq = MAX_RETIREBAG_CAPACITY_POW2; //16384; //32000; //30; //on lines with nbr
#endif
        epoch_freq = 30;

        if (_recoveryMgr)
            COUTATOMIC("SIGRTMIN=" << SIGRTMIN << " neutralizeSignal=" << this->recoveryMgr->neutralizeSignal << std::endl);
        
        retired = new padded<std::list<HeInfo>>[num_process];
        reservations = new padded< std::atomic<uint64_t>* >[num_process];
        for (int i = 0; i < num_process; i++)
        {
            reservations[i].ui = new std::atomic<uint64_t>[NUM_POPHE];
            for (int j = 0; j < NUM_POPHE; j++)
            {
                reservations[i].ui[j].store(0);
            }
        }        
        retire_counters = new padded<uint64_t>[num_process];
        alloc_counters = new padded<uint64_t>[num_process];
        epoch.store(1);
        publishing_epoch.store(0);
    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif   
        srand (time(NULL));     
    }
    ~reclaimer_nbr_popplushe()
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
        // delete [] reservations; //FIXME: is this needed?
        delete [] retire_counters;
        delete [] alloc_counters;
        // std::cout <<"reclaimer destructor finished" <<std::endl;
		COUTATOMIC("epoch_freq= " << epoch_freq <<std::endl<< "empty_freq= " << freq <<std::endl<< "epoch= " <<getEpoch()<<std::endl);
    }
};

#endif //RECLAIM_NBR_POPPLUSHE_H
