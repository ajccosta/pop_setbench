/**
 * The file implements QSBR using POP technique. Helps to avoid a atomic store per operation in QSBR that is incurred to do coarse grained epoch reservation per operation.

 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

// TODO: 
// More perf can be squuezed out"
// 1. If num sigall events could be reduced optimally. Note reducingthis events <1000 cause high mem consumption as reclamation of records happen less often. So there is a sweet spot numsigaall events. Too high > 2k sig all events causes low perf. perf vs mem tradeoff in regards with siggAll events.
// 2. Play with biorth epoch frequency lesser the better as more objects can be freed in empty.. thus less chances when empty is called but objects do not get freed... and repeatedly thread stays in HiWm path..
// 3. Use randomization at lowWm path as I use in nbr+.
// [IMP TODO] 4. Use nbr+ like vector clocks style tests at loWm path insteads of a global atomic variable? To identify Hi Wm sigAll has occured since LoWm was entered.


#ifndef RECLAIM_RCU_POPPLUS_H
#define RECLAIM_RCU_POPPLUS_H

#include <list>
#include "ConcurrentPrimitives.h"
#include "blockbag.h"

// #if !defined HE_ORIGINAL_FREE || !HE_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_rcu_popplus : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int freq;
    int epoch_freq;
    // PAD;
    static const int MAX_RETIREBAG_CAPACITY_POW2 = 32768; //16384; //32768; //16384; //32768; //4096; //8192;//16384;//32768;          //16384;
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

        // vars to learn sigAll events occured and reservations were published by all threads so that a thread could reclaim at loWm path.
        uint64_t reserved_epoch;
        uint64_t saved_publishing_epoch; // at loWm saves the last global TS observed, set by thread at HiWm

        bool firstLoEntryFlag;                   //= true;
        unsigned int numRetiresSinceLoWatermark; // = 0;
        unsigned int LoPathReclaimAttemptFreq;

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
    class RCUPOPInfo
    {
    public:
        T *obj;
        uint64_t retire_epoch;
        RCUPOPInfo(T *obj, uint64_t r_epoch) : obj(obj), retire_epoch(r_epoch) {}
    };

private:
    paddedAtomic<uint64_t> *reservations; // per thread array of reservations 

    // padded<uint64_t> *retire_counters;
    padded<uint64_t> *alloc_counters;
    padded<std::list<RCUPOPInfo>> *retired;

    // PAD;
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
        publishing_epoch.fetch_add(1, std::memory_order_release);
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
        typedef reclaimer_rcu_popplus<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_rcu_popplus<_Tp1, _Tp2> other;
    };

    template <typename First, typename... Rest>
    inline bool startOp(const int tid, void *const *const reclaimers, const int numReclaimers, const bool readOnly = false)
    {
        bool result = true;
        threadData[tid].reserved_epoch = getEpoch();

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
        // going quiscent is not needed as reservtions are only shared upon being signalled
        // ay reclamtion has to first ping so that all thread will reserve latest epoch they are executing in. So, old reservatin sshoul dnever be impact any thread's reclamation. As those thread will only access these reservations after pinging.

        // No, I think this is useful as if signal was received outside ds op and the receipient thread was slow then it woud not be participating in consensus helping to reclaim more if its reserved epoch happened to be the min epoch. Atleast max bag size will reduce in most cases.
        // reservations[tid].ui.store(UINT64_MAX, std::memory_order_release);
    
        // I want to avoid above SC store here. As in pop my contention is that we can avoid global reservation writes by not publishing reservation until pinged.

        threadData[tid].reserved_epoch = UINT64_MAX;
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

    inline void setLoWmMetaData(const int tid)
    {
        threadData[tid].firstLoEntryFlag = false;

        // approach3: 1, 5, 10, 15....50%
        // setLoWmTryReclaimFrequency();
        int rand_val = (rand()%11);
        threadData[tid].LoPathReclaimAttemptFreq = (rand_val==0)? (0.1*MAX_RETIREBAG_CAPACITY_POW2) : (unsigned int)((((rand_val*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);

        // threadData[tid].LoPathReclaimAttemptFreq = 100; //30;
    }

    inline void resetLoWmMetaData(const int tid)
    {
        threadData[tid].firstLoEntryFlag = true;
        threadData[tid].numRetiresSinceLoWatermark=0;
    }

    inline bool isPastLoWatermark(const int tid, size_t myTrashSize = 0)
    {

        return (
            (myTrashSize >= threadData[tid].bagCapacityThreshold/2) 
            && 
            ((threadData[tid].numRetiresSinceLoWatermark % threadData[tid].LoPathReclaimAttemptFreq) == 0)
            );
    }

    inline bool isOutOfPatience(const int tid, size_t myTrashSize = 0)
    {
        return (
            (myTrashSize >= threadData[tid].bagCapacityThreshold)
            &&
            ((threadData[tid].retires_since_HiWm)%8192 /* 512 */ == 0)
            );
    }

    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
        assert(obj && "object to be retired in NULL");

        std::list<RCUPOPInfo> *myTrash = &(retired[tid].ui);
        uint64_t retire_epoch = getEpoch();
        myTrash->push_back(RCUPOPInfo(obj, retire_epoch));
        size_t myTrashSize = myTrash->size();

        // retire_counters[tid] = retire_counters[tid] + 1;

        // use threadData[tid].bagCapacityThreshold for random out of patience thresholds

        // (++(threadData[tid].retires_since_HiWm) > 10 ) ==> this condition helps to avoid costly try signalling and empty ops right after previous reclamation attempt where none of retired objects were eligible due to all retired in epoch grater than minimum reserved epoch. So this thread shall wait for a few ops so that mimimum reserved epoch publishd can get higher than the retire epochs of retired objects in limboBag.
        if (isOutOfPatience(tid, myTrashSize))
        {

            // if bagthreshold has been crossed then ping all threads to publish there reservations. If 0 or a very few records were reclaimed then bag will get full earlier the next time and signal overhead will be incurred. To avoid this ie the frequent signalling overhead we should only attenmpt signalling once atleast half of max size more have been retired ad hope that this time published reserved epochs let us reclaim mor objects. This trades high mem consumption with low sigoverhead. 
            // signall all threads so that they can publish their reserved epochs.
            for (int i = 0; i < num_process; i++){
                threadData[tid].myscannedTS[i] = threadData[i].mypublishingTS.load(std::memory_order_acquire);
            }

            if (requestAllThreadsToRestart(tid))
            {   
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

                bool empty_result = empty(tid);
                #ifdef USE_GSTATS
                    GSTATS_APPEND(tid, reclamation_event_size, myTrashSize);
                #endif
                //reset retires_since_HiWm
                if (empty_result) 
                {
                    threadData[tid].retires_since_HiWm = 0;
                }
                else 
                {
                    ++threadData[tid].retires_since_HiWm;
                }
                resetLoWmMetaData(tid);

#ifdef POPPLUS_DEBUG
                if (tid == 2) COUTATOMICTID("empty invoked at HiWm! firstLoEntryFlag=" << threadData[tid].firstLoEntryFlag << " bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold <<" numRetiresSinceLoWatermark=" << threadData[tid].numRetiresSinceLoWatermark <<" retires_since_HiWm="<<threadData[tid].retires_since_HiWm <<" saved_publishing_epoch="<<threadData[tid].saved_publishing_epoch<<" EPOCH="<<epoch.load(std::memory_order_acquire)<<" trash size="<<myTrash->size()<<std::endl);
#endif                
            }
        
#ifdef POPPLUS_DEBUG
            if (tid == 2) COUTATOMICTID("reclaiming objects in HiWm path! retires_since_HiWm=" << threadData[tid].retires_since_HiWm << " bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold <<" saved_publishing_epoch="<<threadData[tid].saved_publishing_epoch<<"trash size="<<myTrash->size()<<std::endl);
#endif        
        }
        else if (isPastLoWatermark(tid, myTrashSize))
        {
            if(threadData[tid].firstLoEntryFlag){
                setLoWmMetaData(tid);
            }

            // check if new signal was sent
            int64_t new_publishing_epoch = publishing_epoch.load(std::memory_order_acquire);
            if (threadData[tid].saved_publishing_epoch != new_publishing_epoch)
            {
                threadData[tid].saved_publishing_epoch = new_publishing_epoch;

                //reclaim
                #ifdef USE_GSTATS
                    GSTATS_APPEND(tid, lo_reclamation_event_size, myTrashSize);
                #endif
                empty(tid);

                resetLoWmMetaData(tid);
                // thread exits LoWm path
#ifdef POPPLUS_DEBUG
                if (tid == 2) COUTATOMICTID("empty invoked at LoWm! firstLoEntryFlag=" << threadData[tid].firstLoEntryFlag << " bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold <<" numRetiresSinceLoWatermark=" << threadData[tid].numRetiresSinceLoWatermark <<" trash size="<<myTrash->size()<<" saved_publishing_epoch="<<threadData[tid].saved_publishing_epoch<<" EPOCH="<<epoch.load(std::memory_order_acquire)<<std::endl);
#endif
            }
#ifdef POPPLUS_DEBUG
            if (tid == 2) COUTATOMICTID("reclaiming objects in LoWm path! numRetiresSinceLoWatermark=" << threadData[tid].numRetiresSinceLoWatermark << " bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold <<" saved_publishing_epoch="<<threadData[tid].saved_publishing_epoch<<" trash size="<<myTrash->size()<<" EPOCH="<<epoch.load(std::memory_order_acquire)<<std::endl);
#endif
        }

        // start counting num retires since a thread entered LoWm path
        if(!threadData[tid].firstLoEntryFlag)
            threadData[tid].numRetiresSinceLoWatermark++;
    }

    bool empty(const int tid)
    {
        uint64_t min_reserved_epoch = UINT64_MAX;
        int mintid = 0;
        for (int i = 0; i < num_process; i++)
        {
            uint64_t res = reservations[i].ui.load(std::memory_order_acquire);
            if (res < min_reserved_epoch)
            {
                min_reserved_epoch = res;
                mintid = i;
            }
        }
        
        // erase safe objects
        std::list<RCUPOPInfo> *myTrash = &(retired[tid].ui);

        uint before_sz = myTrash->size();
        #ifdef USE_GSTATS
            GSTATS_APPEND(tid, reclamation_event_size, before_sz);
        #endif
        // if (0 == tid) COUTATOMICTID("decided to empty! bag size=" << myTrash->size() << " min_reserved_epoch=" << min_reserved_epoch <<std::endl);

        // int delme_num_reclaimed = 0, delme_cntr = 0;
        for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;)
        {
            RCUPOPInfo res = *iterator;
            
            // if (0 == tid && (++delme_cntr < 10 || delme_cntr > before_sz-10)) COUTATOMIC("re="<<res.retire_epoch<<" ");

            if (res.retire_epoch < min_reserved_epoch)
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
                // I can break frm the loop. as all the following objects are guaranteed to have a retire epoch greater than min_reserved_epoch
                // ++iterator;
                //FIXME: PROVE this works?
                break;
            }
        }

#ifdef POPPLUS_DEBUG

        if (2 == tid) COUTATOMICTID("EMPTY: bag size=" << myTrash->size() << " min_reserved_epoch=" << min_reserved_epoch <<" for tid="<<mintid<<std::endl);
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

        threadData[tid].reserved_epoch = UINT64_MAX; // an epoch which cannt overlap with birth and retire epoch of nodes.
        threadData[tid].retires_since_HiWm = 0;
        threadData[tid].saved_publishing_epoch = 0;
        threadData[tid].firstLoEntryFlag = true;
        threadData[tid].numRetiresSinceLoWatermark = 0;
        threadData[tid].LoPathReclaimAttemptFreq = MAX_RETIREBAG_CAPACITY_POW2/3; //default 1/2 of Max bagsize     

        threadData[tid].mypublishingTS = 0;
        threadData[tid].myscannedTS = new unsigned int[num_process * PREFETCH_SIZE_WORDS];                                  
    }
    void deinitThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif
        // COUTATOMICTID("bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold << std::endl);
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
        reservations[tid].ui.store(threadData[tid].reserved_epoch, std::memory_order_release); // FIXME: std::memory_order_relaxed?
        // COUTATOMICTID("re(" <<threadData[tid].reserved_epoch<< ")=" << reservations[tid].ui.load()<<std::endl);
        threadData[tid].mypublishingTS.fetch_add(1, std::memory_order_acq_rel);              

        return;
    }    

    reclaimer_rcu_popplus(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_rcu_popplus helping=" << this->shouldHelp() << std::endl;
        num_process = numProcesses;
        freq = MAX_RETIREBAG_CAPACITY_POW2; //16384; //32768; //30;
        epoch_freq = 50;

        if (_recoveryMgr)
            COUTATOMIC("SIGRTMIN=" << SIGRTMIN << " neutralizeSignal=" << this->recoveryMgr->neutralizeSignal << std::endl);
        
        retired = new padded<std::list<RCUPOPInfo>>[num_process];
        reservations = new paddedAtomic<uint64_t>[numProcesses];

        for (int i = 0; i < num_process; i++)
        {
            reservations[i].ui.store(UINT64_MAX, std::memory_order_release);
            COUTATOMIC("reservations="<<i<<":" << reservations[i].ui.load() << " ");
        }
        // retire_counters = new padded<uint64_t>[num_process];
        alloc_counters = new padded<uint64_t>[num_process];
        epoch.store(0, std::memory_order_release);
        publishing_epoch.store(0, std::memory_order_release);
    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif   
     srand (time(NULL));     
    }
    ~reclaimer_rcu_popplus()
    {
        // std::cout <<"reclaimer destructor started" <<std::endl;
        for (int i = 0; i < num_process; i++)
        {
            // COUTATOMIC(retired[i].ui.size()<<std::endl);
            for (auto iterator = retired[i].ui.begin(), end = retired[i].ui.end(); iterator != end; )
            {
                RCUPOPInfo res = *iterator;
                iterator=retired[i].ui.erase(iterator); //return iterator corresponding to next of last erased item
                this->pool->add(i, res.obj); //reclaim
            }
            // COUTATOMIC("reservations=" << reservations[i].ui.load() << " ");
            // delete [] reservations[i].ui;
        }
		COUTATOMIC("epoch_freq= " << epoch_freq <<std::endl<< "empty_freq= " << freq <<std::endl<< "epoch= " <<getEpoch()<<std::endl);

        delete [] retired;
        // delete [] reservations;
        // delete [] retire_counters;
        delete [] alloc_counters;
        // std::cout <<"reclaimer destructor finished" <<std::endl;
    }
};

#endif //RECLAIM_RCU_POPPLUS_H
