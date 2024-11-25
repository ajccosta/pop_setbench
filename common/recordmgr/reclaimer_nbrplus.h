/*
 * @Author: trbot and @J 
 * @Date: 2020-09-17 10:15:05 
 * @Last Modified by: mikey.zhaopeng
 * @Last Modified time: 2020-09-17 14:36:01
 * @Description: efficient version of NBR where signal overhead is reduced by using low watrermark and highwatermark mechanism.
 * @DEPENDENCY: This file requires signal setup related code: signal registration, and signal handler definition, sigset and siglongjmp buffer defined in recovery_manager.h. 
 * @KNOWN ISSUE: Still Donot know why skipping to collect own HPS causes validation fail in Gtree. Should check if that happens with Lists as well or it occurs for gtree only. 
 */

#ifndef RECLAIMER_NBRPLUS_H
#define RECLAIMER_NBRPLUS_H

#include "blockbag.h"
#include "reclaimer_interface.h"
#include "arraylist.h"
#include "hashtable.h"

// #if !defined NBRPLUS_ORIGINAL_FREE || !NBRPLUS_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_nbrplus : public reclaimer_interface<T, Pool>
{
private:
    //algorithm specific macros
    static const int MAX_PER_THREAD_HAZARDPTR = 3;                 //3 for guerraoui delete, and 2 for lazy list.
#ifdef LONG_RUNNING_EXP
    static const int MAX_RETIREBAG_CAPACITY_POW2 = 2048;
#else
    static const int MAX_RETIREBAG_CAPACITY_POW2 = 24576;
#endif
    
    // static const int MAX_RETIREBAG_CAPACITY_POW2 = 24576; //16384; //32768; //16384; //32768; //4096; //8192;//16384;//32768;          //16384;
    static const int NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK = 1024; //512; //currently not used//8192 ;//16384;//; //OPTIMIZED_SIGNAL --> set this randomly  with max 1/2 to avoid threads repeatedly entering lopath even after reclaming. ex- at 16k I will reclaim after 16K new records and even after reclaiming 16k at LoWm I still have new 16K in Bag and will enter LoWm path.
    unsigned int num_process;
    PAD;
    padded<uint64_t> *retire_counters;


    //thread local vars and data structures
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
        unsigned int bagCapacityThreshold; //using this variable to set random thresholds for out of patience.

        //BEGIN OPTIMIZED_SIGNAL: LoWatermark variables
        PAD;
        std::atomic<unsigned long long int> announcedTS; //SWMR slot announce ts before and after sending signals, assuming won't overflow.
        PAD;
        unsigned long long int *savedTS; //saves the announcedTS of every other thread

        // no worth in saving exact pointer of reecord in the bag. It is fine to skip reclaiming records in the block and reclaim older blocks following the saved block. Blockbag internally is like a stack.
        std::pair <block<T> *, int > savedRetireBagBlockInfo; 
        // block<T> *savedBagBlockPtr;

        bool firstLoEntryFlag;                   //= true;
        unsigned int numRetiresSinceLoWatermark; // = 0;
        unsigned int LoPathReclaimAttemptFreq;
#ifdef GSTATS_HANDLE_STATS_DELME
        unsigned int LoPathNumConsecutiveAttempts;
#endif
        //END OPTIMIZED_SIGNAL: LoWatermark variables

        ThreadData() {}
    private:
        PAD;
    };

    PAD;
    ThreadData threadData[MAX_THREADS_POW2];
    PAD;

    // #ifdef OPTIMIZED_SIGNAL
    /**
     * 
    */
    inline void setLoWmMetaData(const int tid){
        threadData[tid].firstLoEntryFlag = false;
        setLoWmBookmark(tid);
        setLoWmTryReclaimFrequency();
    
        //take relaxed snapshot of all other announceTS, To be used to know if its time to reclaim at lowpTh. 
        for (int i = 0; i < num_process; i++){
            threadData[tid].savedTS[i] = threadData[i].announcedTS;
        }
    }

    // #ifdef OPTIMIZED_SIGNAL
    /**
     * 
    */
    inline void resetLoWmMetaData(const int tid){
        threadData[tid].firstLoEntryFlag = true;
        threadData[tid].numRetiresSinceLoWatermark=0;
#ifdef GSTATS_HANDLE_STATS_DELME        
        threadData[tid].LoPathNumConsecutiveAttempts=0;
#endif
        for (int j = 0; j < num_process; j++){
            threadData[tid].savedTS[j] = 0;
        }
        resetLoWmBookmark(tid);
    }
    
    /**
     * 
    */
    inline bool isPastLoWatermark(const int tid)
    {
        retire_counters[tid] = retire_counters[tid] + 1;
        if (retire_counters[tid] % 1000 == 0)
        {
            #ifdef USE_GSTATS
                GSTATS_APPEND(tid, reclamation_event_size, ((threadData[tid].retiredBag->getSizeInBlocks() - 1) * BLOCK_SIZE + threadData[tid].retiredBag->getHeadSize()));
            #endif                            
        }

        // TRACE COUTATOMICTID("NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK" << NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK << std::endl);

        //Can avoid calculting size again once already in lowpath. But cost is just 2 function calls of const time. Thats not a big deal.
        return ((((threadData[tid].retiredBag->getSizeInBlocks() - 1) * BLOCK_SIZE + threadData[tid].retiredBag->getHeadSize()) > (threadData[tid].bagCapacityThreshold / 2)) && ((threadData[tid].numRetiresSinceLoWatermark % threadData[tid].LoPathReclaimAttemptFreq) == 0)); //TODO: truly randomize lowWm threshold

        // return (((retiredBag[tid]->getSizeInBlocks() > (temp_patience * (1.0 / (float)((tid % 3) + 2))))) && ((numRetiresSinceLoWatermark % NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK) == 0)); //bag is 1/2, 1/3, 1/4full.
    }

    /**
     * 
    */
    inline void setLoWmBookmark(const int tid)
    {
        //the subsequent blocks could be freed inLoWatermark path. It is an approximate solution as records in saved block would be skipped from reclamation.
        threadData[tid].savedRetireBagBlockInfo = std::make_pair( (threadData[tid].retiredBag->begin()).getCurr(), (threadData[tid].retiredBag->begin()).getIndex() ) ;
    }

    /**
     * randomly set try reclaim frequncy at Lo watermark Path. This shall improve: overhead of LoWm path freq stays low as not every thread frequently incurs lowpth overhead to reclaim and then realise that relaxed quiescenec hasnt occured. That is a wasteful costly checking. I wanna Maximize the chance of reclamaing in lowpath with minum number of reclaim attempts. Returns tryreclaimFreq with values 10, 20, 30, 40, 50 % of Max Bag size.
     * Approach 1 and 2 work good.    
    */
    inline void setLoWmTryReclaimFrequency()
    {
        // COUTATOMICTID("rand freq="<< (unsigned int)((((rand()%5+1))/10.0 )*MAX_RETIREBAG_CAPACITY_POW2)<<std::endl );
        // Good perf needs some threads with freq 1/2MAxBagsize  Why? If have S=1-4 then tput at 18-54 threads is better. If I include 1-4 then tput at 18-54 th is bad but goood by 10% at 90-144 threads..... Why 
        
        // approach1: All threads are equallly likely to take one of 5 frequencies.
        // threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)((((rand()%5+1))/10.0 )*MAX_RETIREBAG_CAPACITY_POW2);//( (rand()%5+1)/10 )*MAX_RETIREBAG_CAPACITY_POW2;

        // approach2: have a bias towards lower values ie more threads choose high attempts freq. 80% percent threads have high reclaim freq. With 70% perf is a little better. 
        // int bias_value = rand()%10;
        // if (bias_value < 7){ //10% 20% 30%
        //     threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)((((rand()%3+1))/10.0 )*MAX_RETIREBAG_CAPACITY_POW2);//( (rand()%5+1)/10 )*MAX_RETIREBAG_CAPACITY_POW2;
        // }
        // else{// 40%, 50%. 20% percent threads have low reclaim freq. reclaim less freq and reclaim big chunk 
        //     threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)((((rand()%2+4))/10.0 )*MAX_RETIREBAG_CAPACITY_POW2);//( (rand()%5+1)/10 )*MAX_RETIREBAG_CAPACITY_POW2;
        // }

        // approach3: 1, 5, 10, 15....50%
        int rand_val = (rand()%11);
        threadData[tid].LoPathReclaimAttemptFreq = (rand_val==0)? (0.01*MAX_RETIREBAG_CAPACITY_POW2) : (unsigned int)((((rand_val*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);

        // approach4: 1, 5, 10, 15....50%
        // int bias_value = rand()%10;
        // if (bias_value < 3){ //80% threads choose high attempts freuency (1%=100 and 5%=20 for 32K max bag size)
        //     int rand_val = (rand()%2);
        //     threadData[tid].LoPathReclaimAttemptFreq = (rand_val==0)? (0.01*MAX_RETIREBAG_CAPACITY_POW2) : (unsigned int)((((rand_val*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);
        // }
        // else{// only 20% of threads choose low attempt freq of 10%=10, 15%=7...50%=2 of bag size 
        //     int rand_val = (rand()%9);
        //     threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)(((((rand_val+2)*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);
        // }


        // int rand_val = (rand()%3);
        // if (rand_val == 0)
        //     threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)(((((5)*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);
        // else if (rand_val == 1)
        //     threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)(((((6)*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);
        // if (rand_val == 2)
        //     threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)(((((10)*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);

        // int bias_value = rand()%10;
        // if (bias_value < 9){ //80% threads choose high attempts freuency (1%=100 and 5%=20 for 32K max bag size)
        //     int rand_val = (rand()%8)+1;
        //     threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)((((rand_val*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);
        // }
        // else{// 
        //     int rand_val = (rand()%2);
        //     threadData[tid].LoPathReclaimAttemptFreq = (unsigned int)(((((rand_val+9)*5))/100.0 )*MAX_RETIREBAG_CAPACITY_POW2);
        // }



        // COUTATOMICTID("rand freq="<< threadData[tid].LoPathReclaimAttemptFreq<<std::endl );


        // int pickrand_tid = rand()%num_process;

    }

    /**
     * 
    */
    inline void resetLoWmBookmark(const int tid)
    {
        //the subsequent blocks could be freed inLoWatermark path. It is an approximate solution as records in saved block would be skipped from reclamation.
        threadData[tid].savedRetireBagBlockInfo = std::make_pair(nullptr, -1);
    }
    // #endif

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

        // TRACE COUTATOMICTID("bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold <<std::endl);
        return (((threadData[tid].retiredBag->getSizeInBlocks() - 1) * BLOCK_SIZE + threadData[tid].retiredBag->getHeadSize()) > threadData[tid].bagCapacityThreshold);
    }

    /**
     * Whenever a thread's retirebag reaches the threshold size (MAX_RETIREBAG_CAPACITY_POW2) then it sends signals to all other threads in the system 
     * using pthread_kill(). Thus invoking NBRsighandler() in recoverymanager.h 
    */
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
                // DEBUG COUTATOMICTID("DEBUG_TID_MAP::"
                //                     << " tid=" << tid << " with pid=" << pthread_self() << " registeredThreads[" << tid << "]=" << registeredThreads[tid] << " sending sig to tid= " << otherTid << " with pid=" << otherPthread << " registeredThreads[" << otherTid << "]=" << registeredThreads[otherTid] << std::endl);

                // assert(debug_main_thread_pid != otherPthread);
                // assert(debug_main_thread_pid != registeredThreads[otherTid]);

                if (error = pthread_kill(otherPthread, this->recoveryMgr->neutralizeSignal))
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
            unsigned int sz = threadData[otherTid].proposedHzptrs->size(); //size shouldn't change during execution.
            assert("prposedHzptr[othertid] should be less than max" && sz <= MAX_PER_THREAD_HAZARDPTR);

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
    inline void sendFreeableRecordsToPool(const int tid, blockbag<T> *const freeable, blockbag<T> *const spareMeBag)
    {
        //get apointer to retirbag of current thread
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
    }

    /**
     * two step process to empty the retire bag: 1) collects all records that are not HP protected (freeable records) and then 2) frees them (add to pool).
    */
    inline bool reclaimFreeable(const int tid)
    {
        bool result = false;
        collectAllSavedRecords(tid);
        sendFreeableRecordsToPool(tid);
        return true;
    }


    inline bool HiWmreclaimFreeable(const int tid)
    {
        bool result = false;
        collectAllSavedRecords(tid);

        blockbag<T> *const freeable = threadData[tid].retiredBag;

        //note calling computeSize() is slow as it needs to linearly traverse of the bas to find size.
        // TRACE COUTATOMICTID("HiWmreclaimFreeable:: Before freeing retiredBag size in nodes=" << threadData[tid].retiredBag->computeSize() << std::endl);

        blockbag<T> *const spareMeBag = new blockbag<T>(tid, this->pool->blockpools[tid]); //use spareMeBag to save all records that should not be reclaimed: 1) HP protected or retired after entering LoWm.

        sendFreeableRecordsToPool(tid, freeable, spareMeBag);

        // TRACE COUTATOMICTID("HiWmreclaimFreeable:: After freeing retiredBag size in nodes=" << threadData[tid].retiredBag->computeSize() << std::endl);
        delete spareMeBag; //FIXME: Couldslow down perf. Can I avoid alloc dealloc?
        return true;
    }

    /**
     * two step process to empty the retire bag: 1) collects all records that are not HP protected (freeable records) and then 2) frees them (add to pool).
    */
    inline bool LoWmreclaimFreeable(const int tid)
    {
        bool result = false;
        collectAllSavedRecords(tid);
#ifdef GSTATS_HANDLE_STATS_DELME
        int num_eligible = 0;
        int num_reclaimed = 0;
#endif

        //get apointer to retirbag of current thread
        blockbag<T> *const freeable = threadData[tid].retiredBag;

// Warning: shall be disabled before perf measurement as its only for perf debug.
#ifdef GSTATS_HANDLE_STATS_DELME
        num_eligible = (freeable->isEmpty() ? 0 : (freeable->getSizeInBlocks()-1)*BLOCK_SIZE + freeable->getHeadSize());
#endif

        blockbag<T> *const spareMeBag = new blockbag<T>(tid, this->pool->blockpools[tid]); //use spareMeBag to save all hazard pointer protected records or the records retired after entering LoWm path.
        T *ptr;
        //one by one remove the records from retireBag. Free it if not Hp protected else add it to spareMeBag.
        assert("LowWm BagHeadNot saved!" && threadData[tid].savedRetireBagBlockInfo.first != nullptr && threadData[tid].savedRetireBagBlockInfo.second != -1);
        // if (threadData[tid].savedRetireBagBlockInfo.first != nullptr && threadData[tid].savedRetireBagBlockInfo.second != -1){
        // TRACE COUTATOMICTID("LoWmreclaimFreeable:: Before freeing retiredBag size in nodes=" << threadData[tid].retiredBag->computeSize() << std::endl);

        //lowWm path reclamation
        while( (freeable->begin().getCurr() != threadData[tid].savedRetireBagBlockInfo.first) || (freeable->begin().getIndex() > threadData[tid].savedRetireBagBlockInfo.second)){
            ptr = freeable->remove();
            spareMeBag->add(ptr);
        }//while()
        // }

        sendFreeableRecordsToPool(tid, freeable, spareMeBag);

#ifdef GSTATS_HANDLE_STATS_DELME
        int num_spared = (freeable->isEmpty() ? 0 : (freeable->getSizeInBlocks()-1)*BLOCK_SIZE + freeable->getHeadSize());
        num_reclaimed = num_eligible - num_spared;
        // GSTATS_APPEND(tid, lowm_num_reclaimed, num_reclaimed); //commented this as using switch bag size for hybrid comaprison.
        GSTATS_APPEND(tid, bagsize_switch_fast_to_slow, num_reclaimed); // for nbrplus represents size reclaimed in fast path. For hybrid reps avg size at which switch occurs and that is reclaimed in slow path.. 

        GSTATS_APPEND(tid, lowm_num_spared, num_spared);
        GSTATS_APPEND(tid, rotate_event_currbag_size, num_eligible);

#endif

        // TRACE COUTATOMICTID("LoWmreclaimFreeable:: After freeing retiredBag size in nodes=" << threadData[tid].retiredBag->computeSize() << std::endl);
        // COUTATOMICTID("LoWmreclaimFreeable::reclaimed bag AttemptFreq="<<threadData[tid].LoPathReclaimAttemptFreq<<" OpsSinceLoPathEntry="<<threadData[tid].numRetiresSinceLoWatermark<<std::endl);
        delete spareMeBag; //FIXME: Couldslow down perf. Can I avoid alloc dealloc?
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
        assert("restartable value should be 0 in before startOp. Check NBR usage rules." && restartable == 0);
        threadData[tid].proposedHzptrs->clear();
        assert("proposedHzptrs->size should be 0" && threadData[tid].proposedHzptrs->size() == 0);

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
            // TRACE COUTATOMICTID("retire:: outOfPatience: retiredBag BlockSize=" << threadData[tid].retiredBag->getSizeInBlocks() << "retiredBag Size nodes=" << threadData[tid].retiredBag->computeSize() << std::endl);

            // #ifdef OPTIMIZED_SIGNAL
            std::atomic_fetch_add(&threadData[tid].announcedTS, 1llu); //tell other threads that I am starting signalling.

            if (requestAllThreadsToRestart(tid))
            {
                // TRACE COUTATOMICTID("retire:: outOfPatience: restarted all threads, gonna continue reclaim =" << threadData[tid].retiredBag->getSizeInBlocks() << " block" << std::endl);
                
                // #ifdef OPTIMIZED_SIGNAL
                std::atomic_fetch_add(&threadData[tid].announcedTS, 1LLU); //tell other threads that I am done signalling
                
                // resetLoWmBookmark(tid); 
                HiWmreclaimFreeable(tid);

                resetLoWmMetaData(tid);
// #ifdef GSTATS_HANDLE_STATS
//         // GSTATS_APPEND(tid, rec_hiwm_count, 1);
//         GSTATS_ADD(tid, num_signal_events, 1);
// #endif
            }
            else
            {
                COUTATOMICTID("retire:: Couldn't restart all threads!" << std::endl);
                assert("Couldn't restart all threads continuing execution could be unsafe ..." && 0);
                exit(-1);
            }
        } // if (isOutOfPatience(tid)){
        else if( isPastLoWatermark(tid) ){
            // TRACE COUTATOMICTID("retire::LowWatermark Path"<<" numRetiresSinceLoWatermark="<<threadData[tid].numRetiresSinceLoWatermark<<std::endl);
#ifdef GSTATS_HANDLE_STATS_DELME
            threadData[tid].LoPathNumConsecutiveAttempts++;
#endif
            //On first entry to Lo path I shall save my baghead. Upto this baghead I can reclaim upon detecting that some one has started and finished signalling after I saved Baghead. That is a condition where all threads have gone Quiescent atleast once after I saved my baghead. 
            if (threadData[tid].firstLoEntryFlag){
                //entered lowWM path for the first time save metadata that would be help to reclaim when someone reaches hiWM
                setLoWmMetaData(tid);
            }
            for (int i = 0; i < num_process; i++){ //TODO: skip self comparison.
                if( threadData[i].announcedTS >= threadData[tid].savedTS[i] + 2){
                    // TRACE COUTATOMICTID("retire:: ******reclaiming atLowWatermark Path*******"<<"announcedTS[i]="<<threadData[i].announcedTS<<"savedTS[i]="<<threadData[tid].savedTS[i]<<std::endl);
// #ifdef GSTATS_HANDLE_STATS
//                     // GSTATS_APPEND(tid, rec_lowm_count, 1);
//                     GSTATS_ADD(tid, num_signal_events, 1);
//                     // GSTATS_APPEND(tid, consecutive_lwm_attempts, threadData[tid].LoPathNumConsecutiveAttempts);
// #endif
                    //reclaim freeable API
                    LoWmreclaimFreeable(tid); //If bag head not null then reclamation shall happen from baghead to tail in api depicting reclamation of lowatermarkpath..

                    resetLoWmMetaData(tid);
                    break;
                }
            }
        } // #ifdef OPTIMIZED_SIGNAL

        if(!threadData[tid].firstLoEntryFlag)
            threadData[tid].numRetiresSinceLoWatermark++;

        assert("retiredBag ds should be non-null" && threadData[tid].retiredBag);
        assert("record to be added should be non-null" && record);
        threadData[tid].retiredBag->add(record);
        // if (tid == 0 ) COUTATOMICTID("retire: curr"<<(threadData[tid].retiredBag->begin()).getCurr()<<"index="<<(threadData[tid].retiredBag->begin()).getIndex()<<std::endl);
    }

    void debugPrintStatus(const int tid)
    {
        TRACE COUTATOMICTID("debugPrintStatus: retiredBag Size in blocks=" << threadData[tid].retiredBag->getSizeInBlocks() << std::endl);
        TRACE COUTATOMICTID("debugPrintStatus: retiredBag Size=" << threadData[tid].retiredBag->computeSize() << std::endl);
        DEBUG_TID_MAP_TRACE COUTATOMICTID("DEBUG_TID_MAP:nbr:debugPrintStatus " << pthread_self() << " registeredThreads[" << tid << "]=" << registeredThreads[tid] << std::endl);
    }

    //  dummy definitions to prevent error/warning of legacy code
    inline bool protect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true){return false;}
    inline void unprotect(const int tid, T* obj){}
    inline bool qProtect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true){return false;}
    inline void enterQuiescentState(const int tid){}
    inline bool leaveQuiescentState(const int tid, void * const * const reclaimers, const int numReclaimers){ return false;}


    //template aliasing
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_nbrplus<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_nbrplus<_Tp1, _Tp2> other;
    };

    /**
     * API that supports harness. Definition is just debug messages. recordmanager, data structure's and record_manager_single_type initThread does the work.
     * Make sure call init deinit pair in setbench harness (main.cpp) to ensure that recovery.h's reseervedThread[] contains correct pthread ids to avoid signalling wrong thread.   
     */
    void initThread(const int tid)
    {
        DEBUG_TID_MAP_TRACE COUTATOMICTID("nbr:initThread::pthread=" << pthread_self() << " registeredThreads[" << tid << "]:" << registeredThreads[tid] << std::endl);

        DEBUG_TID_MAP_TRACE
        {
            // if (pthread_self() != registeredThreads[tid])
            // {
            //     COUTATOMICTID("ERROR(not error if executed first time or first time after last de-init):nbr:deinitThread: This thread=" << pthread_self() << "  is not the one who inited " << " registeredThreads[" << tid << "]=" << registeredThreads[tid] << std::endl);
            //     // assert (0 && "tid and thread id mapping is wrong!");
            // }
            // if (pthread_self() == debug_main_thread_pid)
            // {
            //     COUTATOMICTID("ERROR(if deletes ops are active):nbr: pid getting registered is main thread pid!" << std::endl);
            //     // assert (0 && "could cause neutralization of main thread!");
            // }
        }

        threadData[tid].retiredBag = new blockbag<T>(tid, this->pool->blockpools[tid]);
        threadData[tid].proposedHzptrs = new AtomicArrayList<T>(MAX_PER_THREAD_HAZARDPTR);
        threadData[tid].scannedHzptrs = new hashset_new<T>(num_process * MAX_PER_THREAD_HAZARDPTR);

#ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = new blockbag<T>(tid, this->pool->blockpools[tid]);
        threadData[tid].numFreesPerStartOp = 1;
#endif
        // temp_patience = (MAX_RETIREBAG_CAPACITY_POW2/BLOCK_SIZE) + (tid%5);
        // temp_patience = (MAX_RETIREBAG_CAPACITY_POW2 / BLOCK_SIZE);
        threadData[tid].bagCapacityThreshold = MAX_RETIREBAG_CAPACITY_POW2;

        // #ifdef OPTIMIZED_SIGNAL
        threadData[tid].savedRetireBagBlockInfo = std::make_pair(nullptr, -1);
        // threadData[tid].savedBagBlockPtr = nullptr;
        threadData[tid].announcedTS = 0;
        threadData[tid].savedTS = new unsigned long long int[num_process * PREFETCH_SIZE_WORDS];
        threadData[tid].firstLoEntryFlag = true;
        threadData[tid].numRetiresSinceLoWatermark = 0;
        threadData[tid].LoPathReclaimAttemptFreq = MAX_RETIREBAG_CAPACITY_POW2/2; //default 1/2 of Max bagsize
#ifdef GSTATS_HANDLE_STATS_DELME        
        threadData[tid].LoPathNumConsecutiveAttempts = 0; //default 1/2 of Max bagsize
#endif
        srand (time(NULL));
        
        // #endif
    }

    void deinitThread(const int tid)
    {
        DEBUG_TID_MAP_TRACE COUTATOMICTID("nbr:deinitThread::pthread=" << pthread_self() << " registeredThreads[" << tid << "]=" << registeredThreads[tid] << std::endl);

        DEBUG_TID_MAP_TRACE
        {
            // if (pthread_self() != registeredThreads[tid])
            // {
            //     COUTATOMICTID("ERROR(not error if executed first time or first time after last de-init):nbr:deinitThread: This thread=" << pthread_self() << "  is not the one who inited" << " registeredThreads[" << tid << "]=" << registeredThreads[tid] << std::endl);
            //     // assert (0 && "tid and thread id mapping is wrong!");
            // }
            // if (pthread_self() == debug_main_thread_pid)
            // {
            //     COUTATOMICTID("ERROR()(if deletes ops are active):nbr: pid getting de-inited is main thread pid!" << std::endl);
            //     // assert (0 && "could cause neutralization of main thread");
            // }
        }

        this->pool->addMoveAll(tid, threadData[tid].retiredBag);
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif        

        // #ifdef OPTIMIZED_SIGNAL
        // threadData[tid].savedBagBlockPtr = threadData[tid].retiredBag->end();
        delete[] threadData[tid].savedTS;
        threadData[tid].savedRetireBagBlockInfo = std::make_pair(nullptr, -1);
        // threadData[tid].savedBagBlockPtr = nullptr; //blockbag is reponsible to delete the pointer pointed by savedBagBlockPtr
        // #endif

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
    reclaimer_nbrplus(const int numProcess, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL) : reclaimer_interface<T, Pool>(numProcess, _pool, _debug, _recoveryMgr)
    {

        COUTATOMIC("constructor reclaimer_nbrplus helping=" << this->shouldHelp() << std::endl); // NOTICEME: Not sure why help me should be used here copying d+;
        num_process = numProcess;
        if (_recoveryMgr)
            COUTATOMIC("SIGRTMIN=" << SIGRTMIN << " neutralizeSignal=" << this->recoveryMgr->neutralizeSignal << std::endl);

    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif

        if (MAX_RETIREBAG_CAPACITY_POW2 == 0)
        {
            COUTATOMIC("give a valid value for MAX_RETIREBAG_CAPACITY_POW2!" << std::endl);
            exit(-1);
        }
        retire_counters = new padded<uint64_t>[num_process];

    }

    ~reclaimer_nbrplus()
    {
        COUTATOMIC("bagCapacityThreshold=" << threadData[tid].bagCapacityThreshold << std::endl); // NOTICEME: Not sure why help me should be used here copying d+;
        delete [] retire_counters;

        VERBOSE DEBUG COUTATOMIC("destructor reclaimer_nbrplus" << std::endl);
    }
};

#endif /* RECLAIMER_NBRPLUS_H */
