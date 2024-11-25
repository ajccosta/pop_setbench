/**
 * C++ record manager implementation (PODC 2015) by Trevor Brown.
 * 
 * Copyright (C) 2015 Trevor Brown
 *
 */

#ifndef RECOVERY_MANAGER_H
#define	RECOVERY_MANAGER_H

#include <setjmp.h>
#include <ConcurrentPrimitives.h>
#ifndef VERBOSE
    #define VERBOSE if(0)
#endif
    
#include <cassert>
#include <csignal>
#include "globals.h"
#include "debugcounter.h" //@J to count hanlerexec and siglongjmps

//sig perf testing
#define BEGIN_MEASURE(cycles_high, cycles_low) asm volatile (  "CPUID\n\t"\
                "RDTSC\n\t"\
                "mov %%edx, %0\n\t"\
                "mov %%eax, %1\n\t"\
                :"=r" (cycles_high), "=r" (cycles_low)\
                ::"%rax", "%rbx", "%rcx", "%rdx");

#define END_MEASURE(cycles_high1, cycles_low1) asm volatile (  "RDTSCP\n\t"\
                "mov %%edx, %0\n\t"\
                "mov %%eax, %1\n\t"\
                "CPUID\n\t"\
                :"=r" (cycles_high1), "=r" (cycles_low1)\
                ::"%rax", "%rbx", "%rcx", "%rdx");

// for crash recovery
/*PAD;*/
static pthread_key_t pthreadkey;
static struct sigaction ___act;
static void *___singleton = NULL;
/*PAD;*/
extern pthread_key_t pthreadkey;
extern struct sigaction ___act;
extern void *___singleton;

static pthread_t registeredThreads[MAX_THREADS_POW2];
static void *errnoThreads[MAX_THREADS_POW2];
PAD;
static sigjmp_buf *setjmpbuffers;
PAD;
// volatile thread_local bool restartable = 0; 
thread_local int restartable = 0; // removing volatile for FAA and FE version. I may need to use FAA/CAS in crashhandler as a result.
PAD;
extern pthread_t registeredThreads[MAX_THREADS_POW2];
extern void *errnoThreads[MAX_THREADS_POW2];
// extern sigjmp_buf *setjmpbuffers;

static debugCounter counterNumTimesSignalled(MAX_THREADS_POW2); //@J
static debugCounter countLongjmp(MAX_THREADS_POW2); //@J

//signal optimize var declaration
#ifdef OPTIMIZED_SIGNAL
thread_local bool firstLoEntryFlag = true;
thread_local unsigned int numRetiresSinceLoWatermark = 0;
#endif
#define JUMPBUF_PAD 8
#define MAX_THREAD_ADDR 10000

#ifdef CRASH_RECOVERY_USING_SETJMP
#define CHECKPOINT_AND_RUN_UPDATE(tid, finishedbool) \
    if (MasterRecordMgr::supportsCrashRecovery() && sigsetjmp(setjmpbuffers[(tid)], 0)) { \
        recordmgr->endOp((tid)); \
        (finishedbool) = recoverAnyAttemptedSCX((tid), -1); \
        recordmgr->recoveryMgr->unblockCrashRecoverySignal(); \
    } else
#define CHECKPOINT_AND_RUN_QUERY(tid) \
    if (MasterRecordMgr::supportsCrashRecovery() && sigsetjmp(setjmpbuffers[(tid)], 0)) { \
        recordmgr->endOp((tid)); \
        recordmgr->recoveryMgr->unblockCrashRecoverySignal(); \
    } else
#endif

#define CHECKPOINT_TR(tid, recmgr) \
    int ____jump_ret_val; \
    while( ((recmgr)->needsSetJmp()) && (____jump_ret_val = sigsetjmp(setjmpbuffers[(tid)*JUMPBUF_PAD], 0/*not saving sigmask thats costly. So after end of sighandler call explicit unblocking of signal is needed.*/)) ) { \
        (recmgr)->recoveryMgr->unblockCrashRecoverySignal(); \
    } 



    
// warning: this crash recovery code will only work if you've created a SINGLE instance of bst during an execution.
// there are ways to make it work for multiple instances; i just haven't done that.
template <class MasterRecordMgr>
void crashhandler(int signum, siginfo_t *info, void *uctx) {
    MasterRecordMgr * const recordmgr = (MasterRecordMgr * const) ___singleton;
#ifdef SIGHANDLER_IDENTIFY_USING_PTHREAD_GETSPECIFIC
    int tid = (int) ((long) pthread_getspecific(pthreadkey));
#endif
    TRACE COUTATOMICTID("received signal "<<signum<<std::endl);

    // if i'm active (not in a quiescent state), i must throw an exception
    // and clean up after myself, instead of continuing my operation.
    __sync_synchronize();
    if (!recordmgr->isQuiescent(tid)) {
#ifdef PERFORM_RESTART_IN_SIGHANDLER
        recordmgr->endOp(tid);
        __sync_synchronize();
    #ifdef CRASH_RECOVERY_USING_SETJMP
        siglongjmp(setjmpbuffers[tid*JUMPBUF_PAD], 1);
    #endif
#endif
    }
    // otherwise, i simply continue my operation as if nothing happened.
    // this lets me behave nicely when it would be dangerous for me to be
    // restarted (being in a Q state is analogous to having interrupts 
    // disabled in an operating system kernel; however, whereas disabling
    // interrupts blocks other processes' progress, being in a Q state
    // implies that you cannot block the progress of any other thread.)
}


// paddedAtomic<uint64_t> reservations[MAX_THREADS_POW2]; // per thread array of reservations 

// in he_nbr at start of an poeratin each thread locally
// sves the current global epoch. This saves mfence in IBR like or Debra like 
//  reclaimer at everu operation. Just when needed they are published.
// thread_local uint64_t local_epoch_at_start = UINT64_MAX;

#if defined (OOI_POP_RECLAIMERS) || defined (DAOI_POP_RECLAIMERS) || defined (IBR_HP_POP_RECLAIMERS) || defined (IBR_RCU_HP_POP_RECLAIMERS)
// used by pub on ping reclaimers
template <class MasterRecordMgr>
void trcrashhandler(int signum, siginfo_t *info, void *uctx) 
{
    // //USER Warning: printf cout in here with longjmp causes hang
    MasterRecordMgr * const recordmgr = (MasterRecordMgr * const) ___singleton;
    int tid = (int) ((long) pthread_getspecific(pthreadkey));
    
    recordmgr->publishReservations(tid);
    // reservations[tid].ui.store(local_epoch_at_start, std::memory_order_release);
}
#elif defined (NZB_RECLAIMERS)
// used by NBR and NBR+
template <class MasterRecordMgr>
void trcrashhandler(int signum, siginfo_t *info, void *uctx) 
{
    // //USER Warning: printf cout in here with longjmp causes hang
    // MasterRecordMgr * const recordmgr = (MasterRecordMgr * const) ___singleton;
    MasterRecordMgr * const recordmgr = (MasterRecordMgr * const) ___singleton;
    int tid = (int) ((long) pthread_getspecific(pthreadkey));    
    if(!restartable) {
        return;
    }

    assert ("restartable" && restartable == 1);
    restartable = 0; //if I am CASing restartable in startOP then needs to set 0 here. As CAS compares with old val 0. Not needed if not using CAS.  
    assert ("restartable" && restartable == 0);
    siglongjmp(setjmpbuffers[tid*JUMPBUF_PAD], 1); 
}
#else
template <class MasterRecordMgr>
void trcrashhandler(int signum, siginfo_t *info, void *uctx) 
{
    assert(0 && "this should never be called");
}
#endif

template <class MasterRecordMgr>
class RecoveryMgr {
public:
    PAD;
    const int NUM_PROCESSES;
    const int neutralizeSignal;
    PAD;
    
    inline int getTidInefficient(const pthread_t me) {
        int tid = -1;
        for (int i=0;i<NUM_PROCESSES;++i) {
            if (pthread_equal(registeredThreads[i], me)) {
                tid = i;
            }
        }
        // fail to find my tid -- should be impossible
        if (tid == -1) {
            COUTATOMIC("THIS SHOULD NEVER HAPPEN"<<std::endl);
            assert(false);
            exit(-1);
        }
        return tid;
    }
    inline int getTidInefficientErrno() {
        int tid = -1;
        for (int i=0;i<NUM_PROCESSES;++i) {
            // here, we use the fact that errno is defined to be a thread local variable
            if (&errno == errnoThreads[i]) {
                tid = i;
            }
        }
        // fail to find my tid -- should be impossible
        if (tid == -1) {
            COUTATOMIC("THIS SHOULD NEVER HAPPEN"<<std::endl);
            assert(false);
            exit(-1);
        }
        return tid;
    }
    inline int getTid_pthread_getspecific() {
        void * result = pthread_getspecific(pthreadkey);
        if (!result) {
            assert(false);
            COUTATOMIC("ERROR: failed to get thread id using pthread_getspecific"<<std::endl);
            exit(-1);
        }
        return (int) ((long) result);
    }
    inline pthread_t getPthread(const int tid) {
        TRACE AJDBG COUTATOMICTID("getPthread:: pthreadself:"<<pthread_self()<<" registeredtid:"<<registeredThreads[tid]<<std::endl); //@J
        return registeredThreads[tid];
    }
    
    void initThread(const int tid) {
        if (MasterRecordMgr::supportsCrashRecovery() || MasterRecordMgr::needsSetJmp()) {
            // create mapping between tid and pthread_self for the signal handler
            // and for any thread that neutralizes another
            registeredThreads[tid] = pthread_self();
            
            AJDBG COUTATOMICTID("RECVRY::initThread pthreadself:"<<pthread_self()<<" registeredtid:"<<registeredThreads[tid]<<std::endl); //@J
            // here, we use the fact that errno is defined to be a thread local variable
            errnoThreads[tid] = &errno;
            if (pthread_setspecific(pthreadkey, (void*) (long) tid)) {
                COUTATOMIC("ERROR: failure of pthread_setspecific for tid="<<tid<<std::endl);
            }
            const long __readtid = (long) ((int *) pthread_getspecific(pthreadkey));
            VERBOSE DEBUG COUTATOMICTID("did pthread_setspecific, pthread_getspecific of "<<__readtid<<std::endl);
            assert(__readtid == tid);
        }
    }
    void deinitThread(const int tid) {
        AJDBG COUTATOMICTID("RECVRY::deinitThread pthreadself:"<<pthread_self()<<" registeredtid:"<<registeredThreads[tid]<<std::endl); //@J
        if (MasterRecordMgr::needsSetJmp()) 
            assert (pthread_self() == registeredThreads[tid] && "LOL, tid's mismatch is deadly for TR");
    }
    
    void unblockCrashRecoverySignal() {
        if (MasterRecordMgr::supportsCrashRecovery() || MasterRecordMgr::needsSetJmp() ) {
            __sync_synchronize();
            sigset_t oldset;
            sigemptyset(&oldset);
            sigaddset(&oldset, neutralizeSignal);
            if (pthread_sigmask(SIG_UNBLOCK, &oldset, NULL)) {
                VERBOSE COUTATOMIC("ERROR UNBLOCKING SIGNAL"<<std::endl);
                exit(-1);
            }
        }
    }
    
    RecoveryMgr(const int numProcesses, const int _neutralizeSignal, MasterRecordMgr * const masterRecordMgr)
            : NUM_PROCESSES(numProcesses) , neutralizeSignal(_neutralizeSignal){
        
        if (MasterRecordMgr::supportsCrashRecovery() || MasterRecordMgr::needsSetJmp()) {
            setjmpbuffers = new sigjmp_buf[numProcesses*JUMPBUF_PAD];
            pthread_key_create(&pthreadkey, NULL);
        
            // set up crash recovery signal handling for this process
            memset(&___act, 0, sizeof(___act));
            if(MasterRecordMgr::supportsCrashRecovery())
                ___act.sa_sigaction = crashhandler<MasterRecordMgr>; // specify signal handler
            else if (MasterRecordMgr::needsSetJmp())
            {
                //register signal handler. we use macro for diff defiition for nbr and pubonping
                ___act.sa_sigaction = trcrashhandler<MasterRecordMgr>; // specify signal handler
            }
            else
            {
                COUTATOMIC("ERROR: crash handler not specified?"<<std::endl);
                exit(-1);
            }
            
            ___act.sa_flags = SA_RESTART | SA_SIGINFO; // restart any interrupted sys calls instead of silently failing
            sigfillset(&___act.sa_mask);               // block signals during handler
            if (sigaction(_neutralizeSignal, &___act, NULL)) {
                COUTATOMIC("ERROR: could not register signal handler for signal "<<_neutralizeSignal<<std::endl);
                assert(false);
                exit(-1);
            } else {
                VERBOSE COUTATOMIC("registered signal "<<_neutralizeSignal<<" for crash recovery"<<std::endl);
            }
            
            // set up shared pointer to this class instance for the signal handler
            ___singleton = (void *) masterRecordMgr;
        }
    }
    ~RecoveryMgr() {
        if (MasterRecordMgr::supportsCrashRecovery() || MasterRecordMgr::needsSetJmp() ) {
            delete[] setjmpbuffers;
        }
    }
};

#endif	/* RECOVERY_MANAGER_H */

