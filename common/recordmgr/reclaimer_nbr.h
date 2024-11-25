/* RECLAIMER NBR.h
 *   by trbot and @J
 *
 * Created:
 *   9/1/2020, 8:57:21 PM (Actually created around Dec 2019)
 * Last edited:
 *   9/1/2020, 8:59:37 PM
 *
 * Description:
 *   safe memory reclamation algorithm based on signal based neutralization.
 * Instructions to users:
 *   A data structure operation should be viewed in two phases: 1) search or traversal phase where a thread discovers new pointers enroute its target location over a data structure. Think of traversal over a lazylist. No write operations occur during this phase. 2) update phase where a thread having discovered the target location (for example pred and curr in a lazylist) prepares to write in data structure, for ex using CAS or FAA.
 *   At the beginning of search phase call startOp(). When search phase ends protect the discovered pointers (for ex, in case of the lazylist discovered pointers are pred and curr) using saveForWritePhase() and upgradeToWritePhase(). The upgradeToWritePhase() separates the two aforementioned phases of the ds operation. Ensure that post invocation of upgradeToWritePhase() a unprotected record is not discovered. This is required to maintain safety property of the NBR algorithm.)     
 *  A DS operation shall not invoke system calls in search phase. Ex, free or mallocs in search phase (or outside write phase) would cause undefined behaviour as a thread in mid of these call could be siglongjmped leading to undefined behaviour.
 *   Whenever the DS operation returns user shall call endOp to mark the end of write phase.
 * DEPENDENCY:
 *   This file requires signal setup related code: signal registration, and signal handler definition, sigset and siglongjmp buffer defined in recovery_manager.h. 
 * KNOWN ISSUE:
 * Still Donot know why skipping to collect own HPS causes validation fail in Gtree.
 *  - Should check if that happens with List tree as well or it occurs for gtree only. 
**/

#ifndef RECLAIMER_NBR_H
#define RECLAIMER_NBR_H

#include "blockbag.h"
#include "reclaimer_interface.h"
#include "arraylist.h"
#include "hashtable.h"

// #if !defined NBR_ORIGINAL_FREE || !NBR_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif


template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_nbr : public reclaimer_interface<T, Pool>
{
private:
    //algorithm specific macros
    static const int MAX_PER_THREAD_HAZARDPTR = 4; //4 for abtree viol node       //3 for guerraoui delete, and 2 for lazy list.
    static const int MAX_RETIREBAG_CAPACITY_POW2 = 32768; //32768; //16384; //16384;//32768; //8192; //32768; //16384;

    //local vars and data structures
    unsigned int num_process;
    PAD;

    class ThreadData
    {
    private:
        PAD;

    public:
        blockbag<T> *retiredBag;
#ifdef DEAMORTIZE_FREE_CALLS
        blockbag<T> * deamortizedFreeables;
        int numFreesPerStartOp;
#endif        
        PAD;
        AtomicArrayList<T> *proposedHzptrs;
        PAD;
        hashset_new<T> *scannedHzptrs;
        PAD;
        unsigned int bagCapacityThreshold; // using this variable to set random thresholds for out of patience.

        ThreadData()
        {
        }

    private:
        PAD;
    };

    PAD;
    ThreadData threadData[MAX_THREADS_POW2];
    PAD;

    //REMOVE in Future
    //sigset_t neutralizeSignalSet;
    //PAD;

    /**
     * tells a calling thread whether its retire bag has reached to a threshold size (MAX_RETIREBAG_CAPACITY_POW2) when it's time to empty it.
     * Each threads threshold size is set randomly to avoid all threads reaching size threashold at the same time. Note this is an optimization
     * as it would prevent all threads from bottlenecking whole system by sending signals at the same time.   
    */
    inline bool isOutOfPatience(const int tid)
    {
        // if (temp_patience == 0)
        //     // temp_patience = (MAX_RETIREBAG_CAPACITY_POW2/BLOCK_SIZE) + (tid%5); //#blocks
        //     temp_patience = (MAX_RETIREBAG_CAPACITY_POW2 / BLOCK_SIZE); //#blocks

        // TRACE COUTATOMICTID("bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold << std::endl);
        return (((threadData[tid].retiredBag->getSizeInBlocks() - 1) * BLOCK_SIZE + threadData[tid].retiredBag->getHeadSize()) > threadData[tid].bagCapacityThreshold);
    }

    /**
     * Whenever a thread's retirebag reaches the threshold size (MAX_RETIREBAG_CAPACITY_POW2) then it sends signals to all other threads in the system 
     * using pthread_kill(). Thus invoking NBRsighandler() in recoverymanager.h 
    */
    inline bool requestAllThreadsToRestart(const int tid)
    {
        bool result = false;

        // uint64_t begClock, endClock;
        // unsigned cycles_low, cycles_high, cycles_low1, cycles_high1;

        for (int otherTid = 0; otherTid < this->NUM_PROCESSES; ++otherTid)
        {
            if (tid != otherTid)
            {
                //get posix thread id from application level thread id.
                pthread_t otherPthread = this->recoveryMgr->getPthread(otherTid);
                int error = 0;
                //send signal to other thread
                // COUTATOMICTID("DEBUG_TID_MAP::" << " tid=" << tid << " with pid=" << pthread_self() << " registeredThreads[" << tid << "]=" << registeredThreads[tid] << " sending sig to tid= " << otherTid << " with pid=" << otherPthread << " registeredThreads[" << otherTid << "]=" << registeredThreads[otherTid] << std::endl);

                // BEGIN_MEASURE(cycles_high, cycles_low)
                error = pthread_kill(otherPthread, this->recoveryMgr->neutralizeSignal);
                // END_MEASURE(cycles_high1, cycles_low1)

        //         begClock = ( ((uint64_t)cycles_high << 32) | cycles_low );
        //         endClock = ( ((uint64_t)cycles_high1 << 32) | cycles_low1 );
        //         uint64_t rdtscduration = (endClock - begClock);
        //         // COUTATOMICTID("cyclestosiglast="<< rdtscduration<<std::endl);
        // #ifdef GSTATS_HANDLE_STATS
        //         GSTATS_APPEND(tid, cyclestosigall, rdtscduration);
        // #endif                
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
                //     TRACE COUTATOMICTID(" Signal sent via pthread_kill(pthread_tFor(" << otherTid << "  " << otherPthread << "), " << this->recoveryMgr->neutralizeSignal << ")" << std::endl);
                // }
                // #ifdef GSTATS_HANDLE_STATS
                // GSTATS_ADD(tid, num_signal_events, 1);
                // #endif        
            } // if (tid != otherTid){
        }     // for()

    #ifdef USE_GSTATS
        GSTATS_ADD(tid, signalall, 1);
    #endif        
        result = true;
        return result;
    }

    /**
     * When it's time to empty retirebag a threads first scans all the hazard pointers of all other threads and saves them in a hashtable (scannedHzptrs).
     * This is efficient as now when a thread would free its retired records only O(nk) + O(m) time would be needed, 
     * where m=size of retirebag, n=#threads, k=max hazard pointers per thread.
     * Without this optimization O(mnk) time would be needed to free all record in retirebag.
     *  
    */
    inline void collectAllSavedRecords(const int tid)
    {

        threadData[tid].scannedHzptrs->clear();
        assert("scannedHzptrs size should be 0 before collection" && threadData[tid].scannedHzptrs->size() == 0);

        for (int otherTid = 0; otherTid < this->NUM_PROCESSES; ++otherTid)
        {
            // if (otherTid != tid){ //FIXME: Don't know why skipping to collect own HPs caused gtree validation failure??
            // COUTATOMICTID("begin sz otid="<<otherTid<<" "<<threadData[otherTid].proposedHzptrs<<std::endl);
            // assert(threadData[otherTid].proposedHzptrs && "HP list for this thread is NULL");
            unsigned int sz = threadData[otherTid].proposedHzptrs->size(); //size shouldn't change during execution.
            // assert("prposedHzptr[othertid] should be less than max" && sz <= MAX_PER_THREAD_HAZARDPTR);

            //for each hazard pointer in othertid proposed hazard ptrs.
            for (int ixHP = 0; ixHP < sz; ++ixHP)
            {
                T *hp = (T *)threadData[otherTid].proposedHzptrs->get(ixHP);
                if (hp)
                {
                    threadData[tid].scannedHzptrs->insert((T *)hp);
                }
            }
            // } //if (otherTid != tid)
        }
    }

    /**
     * Frees all records in retireBag exclusing any records which are hazard pointer protected. We use setbenches pool interface to free records currently
     * adding to pool implies that all records are immediately freed as we use pool_none.
     * sigsafe implies that any thread executing this wouldn't be restarted(neutralized) on receiving signals. 
    */
    inline void sendFreeableRecordsToPool(const int tid)
    {
        //get apointer to retirbag of current thread
        blockbag<T> *const freeable = threadData[tid].retiredBag;
        // blockbag_iterator<T> it;
        // = freeable->begin();

        //note calling computeSize() is slow as it needs to linearly traverse of the bas to find size.
        TRACE COUTATOMICTID("TR:: sigSafe: sendFreeableRecordsToPool: Before freeing retiredBag size in nodes=" << threadData[tid].retiredBag->computeSize() << std::endl);
        blockbag<T> *const spareMeBag = new blockbag<T>(tid, this->pool->blockpools[tid]); //use spareMeBag to save all hazard pointer protected records.
        T *ptr;
        //one by one remove the records from retireBag. Free it if not Hp protected else add it to spareMeBag.
        while (1)
        {
            if (freeable->isEmpty())
                break;
            ptr = freeable->remove();
            if (threadData[tid].scannedHzptrs->contains(ptr))
            {
                spareMeBag->add(ptr);
            }
            else
            {
                // here instead of sending to pool, we add to deamortizedFreeables for deamortized freeing
            #ifdef DEAMORTIZE_FREE_CALLS
                threadData[tid].deamortizedFreeables->add(ptr);
            #else
                this->pool->add(tid, ptr);
            #endif


            }
        }

        //add all records that were not freed since they were HP protected back to retireBag.
         while (true)
        {
            if (spareMeBag->isEmpty())
                break;
            ptr = spareMeBag->remove();
            freeable->add(ptr);
        }
        // COUTATOMICTID("TR:: sigSafe: sendFreeableRecordsToPool: After freeing retiredBag size in nodes=" << threadData[tid].retiredBag->computeSize()<< " "<<freeable->computeSize() << std::endl);

        delete spareMeBag;  // FIXME: This could degrade perf Can I avoid newing and deleting sparebag pointer??
    }

    /**
     * two step process to empty the retire bag: 1) collects all records that are not HP protected (freeable records) and then 2) frees them (add to pool).
    */
    inline bool reclaimFreeable(const int tid)
    {
        bool result = false;
        collectAllSavedRecords(tid);
        return true;
    }

public:
    /**
     * public api. Data structure operation should save current thread context using NBR_SIGLONGJMP_TARGET before invoking startOP. As if startOp called in reverse order then a thread could siglongjmp to a previous operation's saved context which could lead to undefined or unsafe executions. 
    */
    template <typename First, typename... Rest>
    inline bool startOp(const int tid, void *const *const reclaimers, const int numReclaimers, const bool readOnly = false)
    {
        bool result = false;
    #ifdef DEAMORTIZE_FREE_CALLS
        // free one object
        if (!threadData[tid].deamortizedFreeables->isEmpty()) {
            this->pool->add(tid, threadData[tid].deamortizedFreeables->remove());
        }
    #endif
        // TRACE COUTATOMICTID("TR:: startOp: NotSigSafe"
        //                     << " typeid(T)=" << typeid(T).name() << std::endl);
        // assert("restartable value should be 0 in before startOp. Check NBR usage rules." && restartable == 0);
        threadData[tid].proposedHzptrs->clear();
        // assert("proposedHzptrs->size should be 0" && threadData[tid].proposedHzptrs->size() == 0);

#ifdef RELAXED_RESTARTABLE_DELME
    restartable = 1;
#elif FAA_RESTARTABLE
    __sync_fetch_and_add(&restartable, 1);
#elif FE_RESTARTABLE
    __sync_lock_test_and_set(&restartable, 1);
#else
    CASB(&restartable, 0, 1); //assert(CASB (&restartable, 0, 1));
#endif
    assert("restartable value should be 1" && restartable == 1);
    result = true;
    return result;
    }

    /**
     * Public API. Protect all the discovered pointers in an operation's search phase before entering the write phase. This ensures that when I am in write phase possibly dereferencing these pointers no other thread could reclaim/free these records.  
    */
    inline void saveForWritePhase(const int tid, T *const record)
    {
        if (!record) return;

        // TRACE COUTATOMICTID("TR:: saveForWritePhase: NotSigSafe"
        //                     << " typeid(T)=" << typeid(T).name() << std::endl);
        assert("proposedHzptrs ds should be non-null" && threadData[tid].proposedHzptrs);
        assert("record to be added should be non-null" && record);
        assert("HzBag is full. Increase MAX_PER_THREAD_HAZARDPTR" && !threadData[tid].proposedHzptrs->isFull());

        threadData[tid].proposedHzptrs->add(record);
    }

    /**
     * after invoking saveForWritePhase() required number of times use this API to imply that a thread entered in writephase thus non-restartable. Meaning that when this thread receives a neutralizing signal it will execute signal handler (NBRsighandler) and would just return to resume what its doing in writephase. 
    */
    inline void upgradeToWritePhase(const int tid)
    {
        // TRACE COUTATOMICTID("TR:: upgradeToWritePhase: SigSafe" << std::endl);
        assert("restartable value should be 1 before write phase" && restartable == 1);
#ifdef RELAXED_RESTARTABLE
        restartable = 0; // writephase instruction wont go in read phase due to proposedHp being atomic
#elif FAA_RESTARTABLE
    __sync_fetch_and_add(&restartable, -1);
#elif FE_RESTARTABLE
    __sync_lock_test_and_set(&restartable, 0);
#else
    CASB(&restartable, 1, 0); //assert (CASB (&restartable, 1, 0));
#endif
        assert("restartable value should be 0 in write phase" && restartable == 0);
    }

    /*
    * Use the API to clear any operation specific ds which won't be required across operations.
    *  i) clear ds populated in saveForWrite
    *  ii) USERWARNING: any record saved for write phase must be released in this API by user. Thus user should call this API at
    *       all control flows that return from dsOP.
    */
    inline void endOp(const int tid)
    {
        // TRACE COUTATOMICTID("TR:: endOp: NotSigSafe"
        //                     << " typeid(T)=" << typeid(T).name() << std::endl);
        assert("proposedHzptrs ds should be non-null" && threadData[tid].proposedHzptrs);

#ifdef RELAXED_RESTARTABLE_DELME
        restartable = 0;
#elif FAA_RESTARTABLE
if (restartable)
{
    __sync_fetch_and_add(&restartable, -1);
}
else
{
    __sync_fetch_and_add(&restartable, 0);
}
#elif FE_RESTARTABLE
    __sync_lock_test_and_set(&restartable, 0);
#else
        CASB(&restartable, 1, 0);
#endif

        assert("restartable value should be 0 in post endOP" && restartable == 0);
    }

    /*
    * Tells whether the reclaimer uses signalling. Use this API to distinguish a NBR specific call using record manager inside a ds operation. You may use this API to tell that the function is meant to invoke NBR's API and skip unnecessarily invoking NBR specific API while using Debra or other reclaimers. 
    */
    inline static bool needsSetJmp()
    {
        return true;
    }

    /**
     * Public API. A ds calls this API after logically deleting (unlinking) a record from DS. It saves the record in retireBag for a delayed free. 
    */
    inline void retire(const int tid, T *record)
    {
        // TRACE COUTATOMICTID("TR:: retire: NotSigSafe"
        //                     << " typeid(T)=" << typeid(T).name() << std::endl);
        if (isOutOfPatience(tid))
        {
            // TRACE COUTATOMICTID("TR:: outOfPatience: SigSafe: retiredBag BlockSize=" << threadData[tid].retiredBag->getSizeInBlocks() << "retiredBag Size nodes=" << threadData[tid].retiredBag->computeSize() << std::endl);
            
            if (requestAllThreadsToRestart(tid))
            {
                // TRACE COUTATOMICTID("TR:: sigSafe: outOfPatience: restarted all threads, gonna continue reclaim =" << threadData[tid].retiredBag->getSizeInBlocks() << " block" << std::endl);
                #ifdef USE_GSTATS
                    GSTATS_APPEND(tid, reclamation_event_size, ((threadData[tid].retiredBag->getSizeInBlocks() - 1) * BLOCK_SIZE + threadData[tid].retiredBag->getHeadSize()));
                #endif                

                // reclaimFreeable(tid);
                collectAllSavedRecords(tid);
                sendFreeableRecordsToPool(tid);
            }
            else
            {
                COUTATOMICTID("TR:: retire: Couldn't restart all threads!" << std::endl);
                assert("Couldn't restart all threads continuing execution could be unsafe ..." && 0);
                exit(-1);
            }
        } // if (isOutOfPatience(tid)){

        assert("retiredBag ds should be non-null" && threadData[tid].retiredBag);
        assert("record to be added should be non-null" && record);
        threadData[tid].retiredBag->add(record);
    }

    void debugPrintStatus(const int tid)
    {
        TRACE COUTATOMICTID("debugPrintStatus: retiredBag Size in blocks=" << threadData[tid].retiredBag->getSizeInBlocks() << std::endl);
        TRACE COUTATOMICTID("debugPrintStatus: retiredBag Size=" << threadData[tid].retiredBag->computeSize() << std::endl);
    }

    //  dummy definitions to prevent error/warning of legacy code
    inline bool protect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true){return false;}
    inline void unprotect(const int tid, T* obj){}


    //template aliasing
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_nbr<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_nbr<_Tp1, _Tp2> other;
    };

    /**
     * API that supports harness. Definition is just debug messages. recordmanager, data structure's and record_manager_single_type initThread does the work.
     * Make sure call init deinit pair in setbench harness (main.cpp) to ensure that recovery.h's reseervedThread[] contains correct pthread ids to avoid signalling wrong thread.   
     */
    void initThread(const int tid)
    {

        // COUTATOMICTID("nbr: initThread\n");
        threadData[tid].retiredBag = new blockbag<T>(tid, this->pool->blockpools[tid]);
        threadData[tid].proposedHzptrs = new AtomicArrayList<T>(MAX_PER_THREAD_HAZARDPTR);
        threadData[tid].scannedHzptrs = new hashset_new<T>(num_process * MAX_PER_THREAD_HAZARDPTR);

#ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = new blockbag<T>(tid, this->pool->blockpools[tid]);
        threadData[tid].numFreesPerStartOp = 1;
#endif

        // temp_patience = (MAX_RETIREBAG_CAPACITY_POW2/BLOCK_SIZE) + (tid%5);
        // temp_patience = (MAX_RETIREBAG_CAPACITY_POW2 / BLOCK_SIZE);
        
        // Jax server specific. Can be generalised by distributing bag cap as per num socket on machine. 
        // // socketnum*2048
        // if (num_process == 1)
        //     threadData[tid].bagCapacityThreshold = 32;
        // else if (num_process > 1 && num_process <=48) //socket 1
        //     threadData[tid].bagCapacityThreshold = 2048;
        // else if (num_process > 48 && num_process <=96) //socket 2
        //     threadData[tid].bagCapacityThreshold = 4096;            
        // else if (num_process > 96 && num_process <=144) //socket 3
        //     threadData[tid].bagCapacityThreshold = 8192;
        // else if (num_process > 144 && num_process <=192) //socket 4
        //     threadData[tid].bagCapacityThreshold = 16384;
        // else
        //     threadData[tid].bagCapacityThreshold = MAX_RETIREBAG_CAPACITY_POW2;
    
        //generate perc from rand number:
        // out of total threads
        if (num_process > 1)
        {
            int num = rand()%100;
            if (num < 5)
                threadData[tid].bagCapacityThreshold = MAX_RETIREBAG_CAPACITY_POW2/8;
            else if (num < 20)
                threadData[tid].bagCapacityThreshold = MAX_RETIREBAG_CAPACITY_POW2/4;
            else if (num < 95)
                threadData[tid].bagCapacityThreshold = MAX_RETIREBAG_CAPACITY_POW2/2;
            else
                threadData[tid].bagCapacityThreshold = MAX_RETIREBAG_CAPACITY_POW2;
        }
        else
        {
            threadData[tid].bagCapacityThreshold = 32;
        }
    }

    void deinitThread(const int tid)
    {
        // COUTATOMICTID("nbr: deinitThread\n");
        // COUTATOMICTID("bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold << std::endl); // NOTICEME: Not sure why help me should be used here copying d+;


        this->pool->addMoveAll(tid, threadData[tid].retiredBag);
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif

        delete threadData[tid].retiredBag;
        delete threadData[tid].proposedHzptrs;
        delete threadData[tid].scannedHzptrs;

        threadData[tid].retiredBag = NULL;
        threadData[tid].proposedHzptrs = NULL;
        threadData[tid].scannedHzptrs = NULL;
    }

    /*
    * CTOR 
    */
    reclaimer_nbr(const int numProcess, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL) : reclaimer_interface<T, Pool>(numProcess, _pool, _debug, _recoveryMgr)
    {

        COUTATOMIC("constructor reclaimer_nbr helping=" << this->shouldHelp() << std::endl); // NOTICEME: Not sure why help me should be used here copying d+;
        num_process = numProcess;
        if (_recoveryMgr)
            COUTATOMIC("SIGRTMIN=" << SIGRTMIN << " neutralizeSignal=" << this->recoveryMgr->neutralizeSignal << std::endl);


    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif

        // set up signal set for neutralize signal
        //NOTICEME: Not sure why doing this as it isnt being used in whole of setbench, but copying debra plus as of now.
        // if (sigemptyset(&neutralizeSignalSet))
        // {
        //     COUTATOMIC("error creating empty signal set" << std::endl);
        //     exit(-1);
        // }
        // if (_recoveryMgr)
        // {
        //     if (sigaddset(&neutralizeSignalSet, this->recoveryMgr->neutralizeSignal))
        //     {
        //         COUTATOMIC("error adding signal to signal set" << std::endl);
        //         exit(-1);
        //     }
        // }

        if (MAX_RETIREBAG_CAPACITY_POW2 == 0)
        {
            COUTATOMIC("give a valid value for MAX_RETIREBAG_CAPACITY_POW2!" << std::endl);
            exit(-1);
        }
    }

    ~reclaimer_nbr()
    {
        // COUTATOMIC("bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold << std::endl); // NOTICEME: Not sure why help me should be used here copying d+;
        
        VERBOSE DEBUG COUTATOMIC("destructor reclaimer_nbr" << std::endl);
        COUTATOMIC("MaxRetireBagCapacity=" << MAX_RETIREBAG_CAPACITY_POW2 << std::endl);
    }
};

#endif /* RECLAIMER_NBR_H */
