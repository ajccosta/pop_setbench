/**
 * Implements NBR and NBR+ ( with MACRO OPTIMIZED_SIGNAL) memory reclamation algorithm.
 * Ajay Singh (@J) and Trevor Brown.
 * Multicore lab uwaterloo
 */

#ifndef RECLAIMER_NBR_ORIG_H
#define RECLAIMER_NBR_ORIG_H

#include <cassert>
#include <iostream>
#include <atomic>

#include "globals.h"
#include "blockbag.h"
#include "arraylist.h"
#include "hashtable.h"

#define OPTIMIZED_SIGNAL


#ifdef OPTIMIZED_SIGNAL
thread_local bool firstLoEntryFlag = true;
thread_local unsigned int numRetiresSinceLoWatermark = 0;
#endif
volatile thread_local uint temp_patience = 0;

template <typename T = void, class Pool = pool_interface<T> >
class reclaimer_nbr_orig : public reclaimer_interface<T, Pool>{
    private:
        //all macros here
        #define MAX_PER_THREAD_HAZARDPTR 3//2.  //3 for guerraoui delete, and 2 for herl;ihy's list.
        #define OPS_BEFORE_NEUTRALIZE 1000
        unsigned int num_process;
        // #define DEAMORTIZE_FREE_CALLS
        // #define DEAMORTIZE_ADAPTIVELY
        PAD;

    class ThreadData {
        private:
            PAD;
        public:
#ifdef DEAMORTIZE_FREE_CALLS
            blockbag<T> * deamortizedFreeables;
            int numFreesPerStartOp;
#endif
            ThreadData() {}
        private:
            PAD;
    };

    PAD;
    ThreadData threadData[MAX_THREADS_POW2];
    PAD;

        blockbag<T> **retiredBag;                                   // retiredBag[tid] = retired records collected by deleteOP
        PAD;
        AtomicArrayList<T> **proposedHzptrs;              // proposedHzptrs[tid] = saves the discovered records  before upgrading to write to protect records from concurrent reclaimer threads.
        PAD;
        hashset_new<T> **scannedHzptrs;                     // scannedHzptrs[tid] =  each reclaimer tid scans HzPtrs across threads to free retired its bag such that anyHzptr aren't freed.
        PAD;

        sigset_t neutralizeSignalSet;
        PAD;

        //signal optimize var declaration
#ifdef OPTIMIZED_SIGNAL
        std::atomic<unsigned long long int> *announcedTS;//SWMR single writer multireader.
        PAD;
        unsigned long long int **savedTS;
        PAD;
        ArrayList<block<T>> **savedBlockHead; //per thread savedBlockHead
        PAD;
#endif

        // this out of patience decision has been improved in the latest NBR+ version on setbench.        
        inline bool isOutOfPatience(const int tid){
            if (temp_patience == 0)
                temp_patience = (MAX_RINGBAG_CAPACITY_POW2/BLOCK_SIZE); //#blocks
            
            TRACE COUTATOMICTID("threshold="<<temp_patience<<std::endl);
            return (retiredBag[tid]->getSizeInBlocks() > temp_patience);
        }

#ifdef OPTIMIZED_SIGNAL
        inline bool isPastLoWatermark(const int tid){
            TRACE COUTATOMICTID("NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK"<<NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK<<std::endl);
            return ( ((retiredBag[tid]->getSizeInBlocks() > ( temp_patience*(1.0 / (float)( (tid%3) + 2) )) )) && ((numRetiresSinceLoWatermark%NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK) == 0) );
        }
        inline void setLoWatermark(const int tid){
            savedBlockHead[tid]->clear();
            savedBlockHead[tid]->add((retiredBag[tid]->begin()).getCurr()); //the subsequent blocks could be freed inLoWatermark path. approximate solution.
        }
#endif
        
        bool requestAllThreadsToRestart(const int tid){
            //pthread_kill to all threads except self
            bool result = false;
            for(int otherTid = 0; otherTid < this->NUM_PROCESSES; ++otherTid){
                //send signal if not self
                if (tid != otherTid){
                    pthread_t otherPthread = this->recoveryMgr->getPthread(otherTid);
                    int error = 0;
                    if( error = pthread_kill (otherPthread, this->recoveryMgr->neutralizeSignal) ){
                        COUTATOMICTID("Error when trying to pthread_kill(pthread_tFor("<<otherTid<<"), "<<this->recoveryMgr->neutralizeSignal<<")"<<std::endl);
                        if(error == ESRCH) COUTATOMICTID("ESRCH"<<std::endl);
                        if (error == EINVAL)  COUTATOMICTID("EINVAL"<<std::endl);
                        assert("Error when trying to pthread_kill" && 0);
                        return result;
                    }
                    else{
                        TRACE COUTATOMICTID(" Signal sent via pthread_kill(pthread_tFor("<<otherTid<<"  " <<otherPthread <<"), "<<this->recoveryMgr->neutralizeSignal<<")"<<std::endl);
                    }                    
                }// if (tid != otherTid){                
            }// for()

#if defined (USE_GSTATS) and defined (MEASURE_TIMELINE_GSTATS) and defined (MEASURE_TG_SIGNAL_ALL_EVENT)
            TIMELINE_GSTATS_BLIP_W(tid, blip_ts_signal_all_event, blip_value_signal_all_event, GSTATS_GET(tid, num_signal_events) );
#endif //#ifdef MEASURE_TIMELINE_GSTATS

            result = true;
            return result;
        }
        
        inline void collectAllSavedRecords(const int tid){
            scannedHzptrs[tid]->clear(); //set where record would be collected in.
            assert("scannedHzptrs[tid] size should be 0 before collection" && scannedHzptrs[tid]->size() == 0);
            
            //OPT: should skip collecting own Hzptrs
            for (int otherTid = 0; otherTid <  this->NUM_PROCESSES; ++otherTid)
            {
                unsigned int sz = proposedHzptrs[otherTid]->size(); //size shouldn't change during execution.
                assert ("prposedHzptr[othertid] should be less than max" && sz <= MAX_PER_THREAD_HAZARDPTR);
                for (int ixHP = 0; ixHP < sz/*Can do this as sz is const*/; ++ixHP){
                    T * hp = (T*) proposedHzptrs[otherTid]->get(ixHP);//acquired_hazardptrs size never changes.
                    if (hp){
                        scannedHzptrs[tid]->insert((T*)hp);
                    }
                }
            }
        }
        
        inline void sendFreeableRecordsToPool(const int tid){
            blockbag<T> * const freeable = retiredBag[tid];

#if defined (USE_GSTATS) and defined (MEASURE_TIMELINE_GSTATS) and defined (MEASURE_TG_RECLAIM_LO_RETIREDBAG)            
            uint sz_before = 0;
            if (!freeable->isEmpty()) {
                sz_before = (freeable->getSizeInBlocks()-1)*BLOCK_SIZE + freeable->getHeadSize();
            }
#endif
            int debug_reclaimed = 0; //REMOVEME
            int numLeftover = 0;
#ifdef DEAMORTIZE_FREE_CALLS
            auto freelist = threadData[tid].deamortizedFreeables;
            if (!freelist->isEmpty()) {
                numLeftover += (freelist->isEmpty() ? 0 : (freelist->getSizeInBlocks()-1)*BLOCK_SIZE + freelist->getHeadSize());

            // // "CATCH-UP" bulk free
            // this->pool->addMoveFullBlocks(tid, freelist);

#if defined DEAMORTIZE_ADAPTIVELY
            // adaptive deamortized free count
            if (numLeftover >= BLOCK_SIZE) {
                ++threadData[tid].numFreesPerStartOp;
            } else if (numLeftover == 0) {
                --threadData[tid].numFreesPerStartOp;
                if (threadData[tid].numFreesPerStartOp < 1) {
                    threadData[tid].numFreesPerStartOp = 1;
                }
            }
#endif 
            }           
#endif

#ifdef OPTIMIZED_SIGNAL
            if(savedBlockHead[tid]->size()){//reclaim due to lowwatermark path
                TRACE COUTATOMICTID("TR:: sigSafe: sendFreeableRecordsToPool: Before freeing retiredBag size in nodes="<<freeable->computeSize()<<std::endl);
                block<T> *savedBlockPtr = savedBlockHead[tid]->get(0);
                blockbag<T> * const sparebag = new blockbag<T>(tid, this->pool->blockpools[tid]);
                blockbag_iterator<T> it =  freeable->begin();

#if defined (USE_GSTATS) and defined (MEASURE_TIMELINE_GSTATS) and defined (MEASURE_TG_RECLAIM_LO_RETIREDBAG)
            TIMELINE_GSTATS_INTERVAL_START_W(tid);
#endif //MEASURE_TG_RECLAIM_LO_RETIREDBAG

                T* ptr;
                while (freeable->begin().getCurr() != savedBlockPtr) //add all records retired after loWatermark to spare bag as these records are not safe to free in LoPath.
                {
                    ptr = freeable->remove();
                    sparebag->add(ptr);
                }
                //now reclaim the bag as it represents all blocks upto low watermark
                //THREAD_DELAY GSTATS_APPEND(tid, limbo_reclamation_event_size, freeable->computeSize());
                //PPOPP GSTATS_ADD(tid, num_reclaimed_in_events, freeable->computeSize());

#ifdef DEAMORTIZE_FREE_CALLS
                freelist->appendMoveFullBlocks(freeable); //send to freelist for de-amortized freeing
#else
                this->pool->addMoveFullBlocks(tid, freeable); //may not reclaim non full blocks
#endif

                //add sparebag that has HP protected records back to freeable aka retiredBag
                while (!sparebag->isEmpty())
                {
                    ptr = sparebag->remove();
                    freeable->add(ptr);
                }
#if defined (USE_GSTATS) and defined (MEASURE_TIMELINE_GSTATS) and defined (MEASURE_TG_RECLAIM_LO_RETIREDBAG)
            uint numRecordsLeft = 0; //records left after reclaiming the bag.
            if (!freeable->isEmpty()) {
                numRecordsLeft = (freeable->getSizeInBlocks()-1)*BLOCK_SIZE + freeable->getHeadSize();
            }
            TIMELINE_GSTATS_INTERVAL_END_W(tid, interval_tsstart_reclaim_lo_retiredbag, interval_tsend_reclaim_lo_retiredbag, interval_value_reclaim_lo_retiredbag, numRecordsLeft );
            TIMELINE_GSTATS_BLIP_W(tid, blip_ts_num_reclaimed_lo_event, blip_value_num_reclaimed_lo_event, (sz_before - numRecordsLeft) );
#endif //MEASURE_TG_RECLAIM_LO_RETIREDBAG

                TRACE COUTATOMICTID("TR:: sigSafe: sendFreeableRecordsToPool: After freeing retiredBag size in nodes="<<retiredBag[tid]->computeSize()<<std::endl);
            }
            else
#endif //OPTIMIZED_SIGNAL
            { //reclaim due to Hiwatermark path
                TRACE COUTATOMICTID("TR:: sigSafe: sendFreeableRecordsToPool: Before freeing retiredBag size in nodes="<<std::endl);
                blockbag<T> * const sparebag = new blockbag<T>(tid, this->pool->blockpools[tid]);//collects HP protected records
                blockbag_iterator<T> it =  freeable->begin();
#if defined (USE_GSTATS) and defined (MEASURE_TIMELINE_GSTATS) and defined (MEASURE_TG_RECLAIM_HI_RETIREDBAG)
            TIMELINE_GSTATS_INTERVAL_START_W(tid);
#endif //MEASURE_TG_RECLAIM_HI_RETIREDBAG

                T* ptr;
                while (1) {                
                    if (freeable->isEmpty()) break;
                    
                    ptr = freeable->remove();
                    if (scannedHzptrs[tid]->contains(ptr))
                    {
                        sparebag->add(ptr);
                    }
                    else
                    {
#ifdef DEAMORTIZE_FREE_CALLS
                        freelist->add(ptr);
#else
                        this->pool->add(tid, ptr);
#endif
                        debug_reclaimed++;
                    }
                }
                
                //THREAD_DELAY GSTATS_APPEND(tid, limbo_reclamation_event_size, debug_reclaimed);
                //PPOPP GSTATS_ADD(tid, num_reclaimed_in_events, debug_reclaimed);

                //PPOPP #ifdef USE_GSTATS 
                //     GSTATS_APPEND(tid, nbr_path_reclamation_event_size, debug_reclaimed);
                //     GSTATS_ADD(tid, nbr_path_num_reclaimed_events, 1);
                // #endif

                //add all collected HP protected records back to freaable aka retiredbag.
                while (1)
                {
                    if (sparebag->isEmpty()) break;
                    ptr = sparebag->remove();
                    freeable->add(ptr);
                }
#if defined (USE_GSTATS) and defined (MEASURE_TIMELINE_GSTATS) and defined (MEASURE_TG_RECLAIM_HI_RETIREDBAG)
            uint numRecordsLeft = 0; //records left after reclaiming the bag.
            if (!freeable->isEmpty()) {
                numRecordsLeft = (freeable->getSizeInBlocks()-1)*BLOCK_SIZE + freeable->getHeadSize();
            }
            TIMELINE_GSTATS_INTERVAL_END_W(tid, interval_tsstart_reclaim_hi_retiredbag, interval_tsend_reclaim_hi_retiredbag, interval_value_reclaim_hi_retiredbag, numRecordsLeft );
#endif //MEASURE_TG_RECLAIM_LO_RETIREDBAG

                TRACE COUTATOMICTID("TR:: sigSafe: sendFreeableRecordsToPool: After freeing retiredBag size in nodes="<<retiredBag[tid]->computeSize()<<std::endl);
            }
        }
        
        //NOTICEME: Currently not freeing descriptors only Nodes record type is freed.
        inline bool reclaimFreeable(const int tid){
            bool result = false;

#if defined (USE_GSTATS) and defined (MEASURE_TIMELINE_GSTATS) and defined (MEASURE_TG_RECLAIM_RETIREDBAG)
            TIMELINE_GSTATS_INTERVAL_START_W(tid);
#endif //MEASURE_TG_RECLAIM_RETIREDBAG

            //collectSavedRecords() in scannedHzptrs[tid] ;
            collectAllSavedRecords(tid); //TODO: For LoPath I donot need to collect records as No thread would hold pointers to my records upto savedHead. Imp perf Boost.            
            //send retired records to Pool to free
            sendFreeableRecordsToPool(tid);            

#if defined (USE_GSTATS) and defined (MEASURE_TIMELINE_GSTATS) and defined (MEASURE_TG_RECLAIM_RETIREDBAG)
            uint numRecordsLeft = 0; //records left after reclaiming the bag.
            if (!retiredBag[tid]->isEmpty()) {
                numRecordsLeft = (retiredBag[tid]->getSizeInBlocks()-1)*BLOCK_SIZE + retiredBag[tid]->getHeadSize();
            }
            TIMELINE_GSTATS_INTERVAL_END_W(tid, interval_tsstart_reclaim_retiredbag, interval_tsend_reclaim_retiredbag, interval_value_reclaim_retiredbag, numRecordsLeft );
#endif //MEASURE_TG_RECLAIM_RETIREDBAG

            return true;
        }
    
    public:
        inline void clearHzBags(const int tid){
            TRACE COUTATOMICTID("TR:: clearHzBags: NotSigSafe"<<" typeid(T)="<<typeid(T).name()<<"HzBagSize="<<proposedHzptrs[tid]->size()<<std::endl);
            proposedHzptrs[tid]->clear();
            assert ("proposedHzptrs[tid]->size should be 0" && proposedHzptrs[tid]->size() == 0);
        }
        
        /*
         * USERWARNING: startOp should always be called after setjmp. Since, if setjmp done later than startOP the restartable would never be set to 1 and Upgrade assert willfail.
         */
        template <typename First, typename... Rest>
        inline bool startOp(const int tid, void * const * const reclaimers, const int numReclaimers, const bool readOnly = false) {
            bool result = false;
            
            TRACE COUTATOMICTID("TR:: startOp: NotSigSafe"<<" typeid(T)="<<typeid(T).name()<<std::endl);
            assert ("restartable value should be 0 in before startOp"&& restartable == 0);

            proposedHzptrs[tid]->clear(); //works only for single type
            assert ("proposedHzptrs[tid]->size should be 0" && proposedHzptrs[tid]->size() == 0);
            
            assert ("restartable should be 0. Else you have violated semantic of OP or b4sigjmp not reseted restartable" && restartable == 0);
#ifdef DEAMORTIZE_FREE_CALLS
            // TODO: make this work for each object type
#if defined DEAMORTIZE_ADAPTIVELY
            for (int i=0;i<threadData[tid].numFreesPerStartOp;++i) {
                if (!threadData[tid].deamortizedFreeables->isEmpty()) {
                    this->pool->add(tid, threadData[tid].deamortizedFreeables->remove());
                } else {
                    break;
                }
            }
#else
            if (!threadData[tid].deamortizedFreeables->isEmpty()) {
                this->pool->add(tid, threadData[tid].deamortizedFreeables->remove());
            }

#endif
#endif //#ifdef DEAMORTIZE_FREE_CALLS

//            restartable = 1; //discovery begin
#ifdef RELAXED_RESTARTABLE
        __sync_lock_test_and_set(&restartable, 1);
#else
            CASB (&restartable, 0, 1);//assert(CASB (&restartable, 0, 1));
#endif
            
            assert ("restartable value should be 1"&& restartable == 1);
            result = true; //can this be reordered before its init?
            return result;
        }
        
        /*
         *  to prevent any records you may want other threads to not reclaim.
         */
        inline void saveForWritePhase(const int tid, T * const record){
            TRACE COUTATOMICTID("TR:: saveForWritePhase: NotSigSafe"<<" typeid(T)="<<typeid(T).name()<<std::endl);
            assert ("proposedHzptrs ds should be non-null" && proposedHzptrs);
            assert ("record to be added should be non-null" && record);
            if (record == NULL)  assert ("null record"&& 0);
            assert ("HzBag IS full" && !proposedHzptrs[tid]->isFull());
            
            proposedHzptrs[tid]->add(record);
        }
        
        /*
         * 
         */
        inline void upgradeToWritePhase(const int tid){
            TRACE COUTATOMICTID("TR:: upgradeToWritePhase: SigSafe"<<std::endl);
           
            assert ("restartable value should be 1 before write phase"&& restartable == 1);
            
#ifdef RELAXED_RESTARTABLE
            __sync_lock_test_and_set(&restartable, 0);
#else
            CASB (&restartable, 1, 0);//assert (CASB (&restartable, 1, 0));
#endif

            assert ("restartable value should be 0 in write phase"&& restartable == 0);
        }
        
        /*
         * Use the API to clear any operation specific ds which won't be required across operations.
         *  i) clear ds populated in saveForWrite
         *  ii) USERWARNING: any record saved for write phase must be released in this API by user. Thus user should call this API at all control flows that return from dsOP.
         */
        inline void endOp(const int tid){
            TRACE COUTATOMICTID("TR:: endOp: NotSigSafe"<<" typeid(T)="<<typeid(T).name()<<std::endl);
            
            assert ("proposedHzptrs ds should be non-null" && proposedHzptrs);
            //not needed to clear in endOp as startOp always clears.
            // proposedHzptrs[tid]->clear(); //@J since this is a simple store 0 even if interrupted or siglongjmped no issues.
            // assert ("proposedHzptrs[tid] should have size 0" && proposedHzptrs[tid]->size() == 0);
            
            // NOTE:ds ops could restart before entering writephase.
            #ifdef RELAXED_RESTARTABLE
            __sync_lock_test_and_set(&restartable, 0);
            // restartable = 0;
            #else
            CASB (&restartable, 1, 0); // NOT needed extra cache miss can be optimized, umm but restartable is thread local, thus not costly. TODO:remove later 
            #endif
            
            assert ("restartable value should be 0 in post endOP"&& restartable == 0);
        }
        
        /*
         * Tells whether the reclaimer supports signalling
        */
        inline static bool needsSetJmp(){
            return true;
        }
        
        /*
         *  retire not only gets called by deletes. But also by helps in ins or lookup. DS may populate Hz bags from helps thus exceed the size limit. Need to protect descpritors as well.
         */
        inline void retire(const int tid, T* record){
            TRACE COUTATOMICTID("TR:: retire: NotSigSafe"<<" typeid(T)="<<typeid(T).name()<<std::endl);
            
            //RECLAIMATION PHASE
            //OPT: getblocksize is faster than computesize in records
            if (isOutOfPatience(tid)){
                 TRACE COUTATOMICTID("TR:: outOfPatience: SigSafe: retiredBag BlockSize="<< retiredBag[tid]->getSizeInBlocks()<<"retiredBag Size nodes="<<retiredBag[tid]->computeSize()<<std::endl);
#ifdef OPTIMIZED_SIGNAL //TODO: Seems I can reduce two FAA to 1.
                std::atomic_fetch_add(&announcedTS[tid], 1llu); //tell other threads that I am starting signalling.
#endif                
                if (requestAllThreadsToRestart(tid)){
                    TRACE COUTATOMICTID("TR:: sigSafe: outOfPatience: restarted all threads, gonna continue reclaim ="<<retiredBag[tid]->getSizeInBlocks()<<" block"<<std::endl);
                    //NOTICEME: only frees one record type. Needs to free all records type.
                    GSTATS_ADD(tid, num_signal_events, 1);
#ifdef OPTIMIZED_SIGNAL
                    std::atomic_fetch_add(&announcedTS[tid], 1LLU); //tell other threads that I am done signalling
                    
                    savedBlockHead[tid]->clear();//full bag shall be reclaimed so clear any bag head. Avoiding changes to arg of this reclaimFreeable api.
#endif
                    reclaimFreeable(tid);
#ifdef OPTIMIZED_SIGNAL
                    firstLoEntryFlag = true;
                    numRetiresSinceLoWatermark = 0;
                    // memset(savedTS, 0, sizeof(savedTS)); //NOTICEME: Am I safe sys call in middle of a sig
                    for (int i = 0; i < num_process; i++){
                        savedTS[tid][i] = 0;
                    }
#endif
                }
                else{
                    COUTATOMICTID("TR:: outOfPatience: Couldn't restart all threads, gonna continue ops"<<std::endl);
                    assert ("Should have restarted all threads..." && 0);
                    exit (-1);
                }
            } // if (isOutOfPatience(tid)){
#ifdef OPTIMIZED_SIGNAL
            else if( isPastLoWatermark(tid) ){
                TRACE COUTATOMICTID("retire::atLowWatermark Path"<<" numRetiresSinceLoWatermark="<<numRetiresSinceLoWatermark<<std::endl);
                //On first entry to Lo path I shall save my baghead. Upto this baghead I can reclaim upon detecting that some one has started and finished signalling after I saved Baghead. That a condition
                //where all threads have gone Quiescent atleast once after I saved my baghead. 
                if (firstLoEntryFlag){
                    firstLoEntryFlag = false;
                    setLoWatermark(tid);
                    //take relaxed snapshot of all other announceTS, To be used to know if its time to reclaim at lowpTh. 
                    for (int i = 0; i < num_process; i++){
                        savedTS[tid][i] = announcedTS[i];
                    }
                }
                for (int i = 0; i < num_process; i++){ //TODO: skip self comparison.
                    if( announcedTS[i] >= savedTS[tid][i] + 2){
                        TRACE COUTATOMICTID("retire:: ******reclaiming atLowWatermark Path*******"<<"announcedTS[i]="<<announcedTS[i]<<"savedTS[tid][i]="<<savedTS[tid][i]<<std::endl);
                        //reclaim freeable API
                        //TODO: reclaimFreeable(tid, baghead);
                        reclaimFreeable(tid); //If bag head not null then reclamation shall happen from baghead to tail in api depicting reclamation of lowatermarkpath..

                        firstLoEntryFlag = true;
                        numRetiresSinceLoWatermark=0;
                        // memset(savedTS, 0, sizeof(savedTS)); //NOTICEME: Am I safe sys call in middle of a sig receipt.
                        for (int j = 0; j < num_process; j++){
                            savedTS[tid][j] = 0;
                        }
                        break;
                    }
                }

            }

            if(!firstLoEntryFlag)
                numRetiresSinceLoWatermark++;
#endif

            assert ("retiredBag ds should be non-null" && retiredBag);
            assert ("record to be added should be non-null" && record);
            if (record == NULL) assert ("null record"&& 0);
            retiredBag[tid]->add(record);                
        }
        
        void debugPrintStatus(const int tid){
            TRACE COUTATOMICTID ("retiredBag Size in blocks:"<<retiredBag[tid]->getSizeInBlocks()<<std::endl);
            TRACE COUTATOMICTID ("retiredBag Size:"<<retiredBag[tid]->computeSize() <<std::endl);
        }
        
        //template aliasing
        template<typename _Tp1>
        struct rebind {
            typedef reclaimer_nbr_orig<_Tp1, Pool> other;
        };
        template<typename _Tp1, typename _Tp2>
        struct rebind2{
            typedef reclaimer_nbr_orig<_Tp1, _Tp2> other;
        };
        
        //dummy definitions to prevent error/warning of legacy code
        inline bool protect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true){return false;}
        inline bool qProtect(const int tid, T* obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true){return false;}
        inline void enterQuiescentState(const int tid){}
        inline bool leaveQuiescentState(const int tid, void * const * const reclaimers, const int numReclaimers){ return false;}
        inline static void unprotect(const int tid, T *const obj) {}
    
        /*
         * Important to define these and not leave them empty. So that registered tid for the recovery.h gets inited. Currently in the setbench code
         *  initTHread and deinit thread does n't get called.
         */
        void initThread(const int tid) {
            AJDBG COUTATOMICTID("tr::pthreadself:"<<pthread_self()<<" registeredtid:"<<registeredThreads[tid]<<std::endl); //@J   
            // temp_patience = (MAX_RINGBAG_CAPACITY_POW2/BLOCK_SIZE) + (tid%5);
            temp_patience = (MAX_RINGBAG_CAPACITY_POW2/BLOCK_SIZE);         
        }
        void deinitThread(const int tid) {
            AJDBG COUTATOMICTID("tr::pthreadself:"<<pthread_self()<<" registeredtid:"<<registeredThreads[tid]<<std::endl); //@J
        }
        //NOTICEME:  if I dont write false, them startOp will be called for each rectype and which will cause startOp assrt to fail. As startOp called twice w/o matching endOP 
        //to reset restaratable to 0.
        inline static bool quiescenceIsPerRecordType() { return false; }
        
        /*
         * CTOR 
         */
        reclaimer_nbr_orig(const int numProcess, Pool *_pool, debugInfo *const  _debug, RecoveryMgr<void*> * const _recoveryMgr = NULL) : reclaimer_interface<T, Pool> (numProcess, _pool, _debug, _recoveryMgr) {
            
            COUTATOMIC("constructor reclaimer_nbr_orig helping="<<this->shouldHelp()<<std::endl);// NOTICEME: Not sure why help me should be used here copying d+;
            num_process = numProcess;
            if (_recoveryMgr) COUTATOMIC("SIGRTMIN="<<SIGRTMIN<<" neutralizeSignal="<<this->recoveryMgr->neutralizeSignal<<std::endl);
            
            // set up signal set for neutralize signal
            //NOTICEME: Not sure why doing this as it isnt being used in whole of setbench, but copying debra plus as of now.
            if (sigemptyset(&neutralizeSignalSet)) {
                COUTATOMIC("error creating empty signal set"<<std::endl);
                exit(-1);
            }
            if (_recoveryMgr) {
                if (sigaddset(&neutralizeSignalSet, this->recoveryMgr->neutralizeSignal)) {
                    COUTATOMIC("error adding signal to signal set"<<std::endl);
                    exit(-1);
                 }
            }

            if (MAX_RINGBAG_CAPACITY_POW2 == 0 || NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK == 0) {
                COUTATOMIC("MAX_RINGBAG_CAPACITY_POW2="<<MAX_RINGBAG_CAPACITY_POW2 <<" NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK="<<NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK<<std::endl);
                COUTATOMIC("give a valid value for NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK & MAX_RINGBAG_CAPACITY_POW2 at cmd line!"<<std::endl);
                exit(-1);
            }
            
            //init all reclaimer ds
            retiredBag = new blockbag<T> * [numProcess*PREFETCH_SIZE_WORDS];
            proposedHzptrs = new AtomicArrayList<T> * [numProcess + 2*PREFETCH_SIZE_WORDS] + PREFETCH_SIZE_WORDS;
            scannedHzptrs = new hashset_new<T> * [numProcess*PREFETCH_SIZE_WORDS];

#ifdef OPTIMIZED_SIGNAL
            savedBlockHead = new ArrayList<block<T>> *[numProcess*PREFETCH_SIZE_WORDS];
            announcedTS = new std::atomic<unsigned long long int> [numProcess*PREFETCH_SIZE_WORDS];
            savedTS = new unsigned long long int * [numProcess*PREFETCH_SIZE_WORDS];
#endif

            for (int tid  = 0; tid < numProcess; ++tid){
                retiredBag[tid] = new blockbag<T>(tid, this->pool->blockpools[tid]);
                proposedHzptrs[tid] = new AtomicArrayList<T>(MAX_PER_THREAD_HAZARDPTR);
                scannedHzptrs[tid] = new hashset_new<T>(numProcess*MAX_PER_THREAD_HAZARDPTR);

#ifdef OPTIMIZED_SIGNAL
                savedBlockHead[tid] = new ArrayList<block<T>>(1);
                announcedTS[tid] = 0;
                savedTS[tid] = new unsigned long long int [numProcess]();
#endif
#ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = new blockbag<T>(tid, this->pool->blockpools[tid]);
        threadData[tid].numFreesPerStartOp = 1;
#endif
            }
            //TODO: assert initial state of each DS
        }
                
        ~reclaimer_nbr_orig(){
           VERBOSE DEBUG COUTATOMIC("destructor reclaimer_nbr_orig"<<std::endl);
           
            for (int tid  = 0; tid < this->NUM_PROCESSES; ++tid){ //NUM_PROCESSES from reclaimer_interface
                //move any remaining records to free.Else dtor of blockbag asserts.
                this->pool->addMoveAll(tid, retiredBag[tid]);
                //call DS dtors
                delete retiredBag[tid];
                delete proposedHzptrs[tid];
                delete scannedHzptrs[tid];
                
#ifdef OPTIMIZED_SIGNAL
                savedBlockHead[tid];
                delete[] savedTS[tid];
#endif
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif

            }
            //delete array 
            delete[] retiredBag;
            delete[] (proposedHzptrs - PREFETCH_SIZE_WORDS);
            delete[] scannedHzptrs;

            #ifdef OPTIMIZED_SIGNAL
            delete[] savedBlockHead;
            delete[] announcedTS;
            delete[] savedTS;
            #endif
            COUTATOMIC("MAX_RINGBAG_CAPACITY_POW2="<<MAX_RINGBAG_CAPACITY_POW2<< ":"<<NUM_OP_BEFORE_TRYRECLAIM_LOWATERMARK<<std::endl);
            // COUTATOMIC("counterNumTimesSignalled="<<counterNumTimesSignalled.getTotal()<<std::endl); //PROFILE_SIGNALS
            // COUTATOMIC("countLongjmp="<<countLongjmp.getTotal()<<std::endl); //PROFILE_SIGNALS
        }
};

#endif /* RECLAIMER_NBR_ORIG_H */

