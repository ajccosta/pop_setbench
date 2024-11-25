/**
 * The code is adapted from https://github.com/rusnikola/wfsmr to make it work with Setbench.
 * Copyright (c) 2020 Ruslan Nikolaev.  All Rights Reserved.
 * 
 * This file implements Wait Free eras(WFE) memory reclamation (https://drops.dagstuhl.de/opus/volltexte/2021/14862/pdf/LIPIcs-DISC-2021-60.pdf).
 * The exact file for WFE is WFETracker.hpp in the wfsmr code.
 * belongs to DAOI_RECLAIMERS bcse needs birth epoch in data (node) and read())
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

#ifndef RECLAIM_WFE_H
#define RECLAIM_WFE_H

#include <atomic>
#include "ConcurrentPrimitives.h"
#include "dcas.h"
#include "blockbag.h"

// #if !defined WFE_ORIGINAL_FREE || !WFE_ORIGINAL_FREE
//     #define DEAMORTIZE_FREE_CALLS
// #endif

#define MAX_WFE		8

union word_pair_t {
	std::atomic<uint64_t> pair[2];
	std::atomic<__uint128_t> full;
};

union value_pair_t {
	uint64_t pair[2];
	__uint128_t full;
};

struct state_t {
	word_pair_t result;
	std::atomic<uint64_t> epoch;
	std::atomic<uint64_t> pointer;
};

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_wfe : public reclaimer_interface<T, Pool>
{
private:
	int num_process;
	int he_num;
	int epoch_freq;
	int freq;
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
	struct WFESlot {
		word_pair_t slot[MAX_WFE];
	};

	struct WFEState {
		state_t state[MAX_WFE];
	};

	struct WFEInfo {
		struct WFEInfo* next;
		uint64_t birth_epoch;
		uint64_t retire_epoch;
	};

private:
	WFESlot* reservations;
	WFESlot* local_reservations;
	WFEState* states;
	padded<uint64_t>* retire_counters;
	padded<uint64_t>* alloc_counters;
	padded<WFEInfo*>* retired;
	paddedAtomic<uint64_t> counter_start, counter_end;

	paddedAtomic<uint64_t> epoch;

public:
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_wfe<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_wfe<_Tp1, _Tp2> other;
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

    //clear_all() in original implementation  
    inline void endOp(const int tid)
    {
		for (int i = 0; i < he_num; i++){
			reservations[tid].slot[i].pair[0].store(0, std::memory_order_seq_cst);
		}
    }

	//before you call this also be careful about initing the reclaimer related variables in allocated node from setbench allocator.
    // inline void updateAllocCounterAndEpoch(const int tid)
    // {
	// 	alloc_counters[tid] = alloc_counters[tid]+1;
	// 	if(alloc_counters[tid]%(epoch_freq*num_process)==0){
	// 		// help other threads first
	// 		help_read(tid);
	// 		// only after that increment the counter
	// 		epoch.ui.fetch_add(1,std::memory_order_acq_rel);
	// 	}

    //     // COUTATOMICTID("epoch="<<epoch.load(std::memory_order_acquire)<<std::endl);
    // }

	T* instrumentedAlloc(const int tid)
	{
		alloc_counters[tid] = alloc_counters[tid]+1;
		if (alloc_counters[tid] % (epoch_freq*num_process) == 0){
			help_read(tid);
			epoch.ui.fetch_add(1, std::memory_order_acq_rel);
		}

		char* block = (char*) malloc(sizeof(WFEInfo) + sizeof(T));
		WFEInfo* info = (WFEInfo*) (block + sizeof(T));
		info->birth_epoch = getEpoch();
		return (T*)block;
	}	

	uint64_t getEpoch() {
		return epoch.ui.load(std::memory_order_acquire);
	}

	inline void help_thread(int tid, int index, int mytid)
	{
		value_pair_t last_result;
		last_result.full = dcas_load(states[tid].state[index].result.full, std::memory_order_acquire);
		if (last_result.pair[0] != (uint64_t) -1LL)
			return;
		uint64_t birth_epoch = states[tid].state[index].epoch.load(std::memory_order_acquire);
		reservations[mytid].slot[he_num].pair[0].store(birth_epoch, std::memory_order_seq_cst);
		std::atomic<T*> *obj = (std::atomic<T*> *) states[tid].state[index].pointer.load(std::memory_order_acquire);
		uint64_t seqno = reservations[tid].slot[index].pair[1].load(std::memory_order_acquire);
		if (last_result.pair[1] == seqno) {
			uint64_t prev_epoch = getEpoch();
			do {
				reservations[mytid].slot[he_num+1].pair[0].store(prev_epoch, std::memory_order_seq_cst);
				T* ptr = obj ? obj->load(std::memory_order_acquire) : nullptr;
				uint64_t curr_epoch = getEpoch();
				if (curr_epoch == prev_epoch) {
					value_pair_t value;
					value.pair[0] = (uint64_t) ptr;
					value.pair[1] = curr_epoch;
					if (dcas_compare_exchange_strong(states[tid].state[index].result.full, last_result.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire)) {
						value.pair[0] = curr_epoch;
						value.pair[1] = seqno + 1;
						value_pair_t old;
						old.pair[1] = reservations[tid].slot[index].pair[1].load(std::memory_order_acquire);
						old.pair[0] = reservations[tid].slot[index].pair[0].load(std::memory_order_acquire);
						do { // 2 iterations at most
							if (old.pair[1] != seqno)
								break;
						} while (!dcas_compare_exchange_weak(reservations[tid].slot[index].full, old.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire));
					}
					break;
				}
				prev_epoch = curr_epoch;
			} while (last_result.full == dcas_load(states[tid].state[index].result.full, std::memory_order_acquire));
			reservations[mytid].slot[he_num+1].pair[0].store(0, std::memory_order_seq_cst);
		}
		reservations[mytid].slot[he_num].pair[0].store(0, std::memory_order_seq_cst);
	}

	inline void help_read(int mytid)
	{
		// locate threads that need helping
		uint64_t ce = counter_end.ui.load(std::memory_order_acquire);
		uint64_t cs = counter_start.ui.load(std::memory_order_acquire);
		if (cs - ce != 0) {
			for (int i = 0; i < num_process; i++) {
				for (int j = 0; j < he_num; j++) {
					uint64_t result_ptr = states[i].state[j].result.pair[0].load(std::memory_order_acquire);
					if (result_ptr == (uint64_t) -1LL) {
						help_thread(i, j, mytid);
					}
				}
			}
		}
	}

    // T* read(int tid, int idx, std::atomic<T*> &obj)
	T* readByPtrToTypeAndPtr(const int tid, int index, std::atomic<T*> &ptrToObj, T* obj) {
		// fast path
		uint64_t prev_epoch = reservations[tid].slot[index].pair[0].load(std::memory_order_acquire);
		size_t attempts = 16;
		do {
			T* ptr = ptrToObj.load(std::memory_order_acquire);
			uint64_t curr_epoch = getEpoch();
			if (curr_epoch == prev_epoch) {
				return ptr;
			} else {
				reservations[tid].slot[index].pair[0].store(curr_epoch, std::memory_order_seq_cst);
				prev_epoch = curr_epoch;
			}
		} while (--attempts != 0);

		return slow_path(&ptrToObj, index, tid, obj);

    }

    //not writing reserve_slot() from original implementation. I think I am not gonna use in any ds.
    // Might be wrong, in case I may need it for some DS in future. 

	__attribute__((noinline)) T* slow_path(std::atomic<T*>* obj, int index, int tid, T* node)
	{
		// slow path
		uint64_t prev_epoch = reservations[tid].slot[index].pair[0].load(std::memory_order_acquire);
		counter_start.ui.fetch_add(1, std::memory_order_acq_rel);
		states[tid].state[index].pointer.store((uint64_t) obj, std::memory_order_release);
		uint64_t birth_epoch = (node == nullptr) ? 0 :
			((WFEInfo *)(node + 1))->birth_epoch;
		states[tid].state[index].epoch.store(birth_epoch, std::memory_order_release);
		uint64_t seqno = reservations[tid].slot[index].pair[1].load(std::memory_order_acquire);
		value_pair_t last_result;
		last_result.pair[0] = (uint64_t) -1LL;
		last_result.pair[1] = seqno;
		dcas_store(states[tid].state[index].result.full, last_result.full, std::memory_order_release);

		uint64_t result_epoch, result_ptr;
		do {
			value_pair_t value, last_epoch;
			T* ptr = obj ? obj->load(std::memory_order_acquire) : nullptr;
			uint64_t curr_epoch = getEpoch();
			if (curr_epoch == prev_epoch) {
				last_result.pair[0] = (uint64_t) -1LL;
				last_result.pair[1] = seqno;
				value.pair[0] = 0;
				value.pair[1] = 0;
				if (dcas_compare_exchange_strong(states[tid].state[index].result.full, last_result.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire)) {
					reservations[tid].slot[index].pair[1].store(seqno + 1, std::memory_order_release);
					counter_end.ui.fetch_add(1, std::memory_order_acq_rel);
					return ptr;
				}
			}
			last_epoch.pair[0] = prev_epoch;
			last_epoch.pair[1] = seqno;
			value.pair[0] = curr_epoch;
			value.pair[1] = seqno;
			dcas_compare_exchange_strong(reservations[tid].slot[index].full, last_epoch.full, value.full, std::memory_order_seq_cst, std::memory_order_acquire);
			prev_epoch = curr_epoch;
			result_ptr = states[tid].state[index].result.pair[0].load(std::memory_order_acquire);
		} while (result_ptr == (uint64_t) -1LL);

		result_epoch = states[tid].state[index].result.pair[1].load(std::memory_order_acquire);
		reservations[tid].slot[index].pair[0].store(result_epoch, std::memory_order_release);
		reservations[tid].slot[index].pair[1].store(seqno + 1, std::memory_order_release);
		counter_end.ui.fetch_add(1, std::memory_order_acq_rel);

		return (T*) result_ptr;
	}


    // for all schemes except reference counting
    inline void retire(const int tid, T *obj)
    {
		if(obj==NULL){return;}
		WFEInfo** field = &(retired[tid].ui);
		WFEInfo* info = (WFEInfo*) (obj + 1);
		info->retire_epoch = epoch.ui.load(std::memory_order_acquire);
		info->next = *field;
		*field = info;
		if(retire_counters[tid]%freq==0){
			empty(tid);
		}
		retire_counters[tid]=retire_counters[tid]+1;
    }

	bool can_delete(WFESlot* local, uint64_t birth_epoch, uint64_t retire_epoch, int js, int je) {
		for (int i = 0; i < num_process; i++){
			for (int j = js; j < je; j++){
				const uint64_t epo = local[i].slot[j].pair[0].load(std::memory_order_acquire);
				if (epo < birth_epoch || epo > retire_epoch || epo == 0){
					continue;
				} else {
					return false;
				}
			}
		}
		return true;
	}

    void empty(const int tid)
    {
		// erase safe objects
		WFESlot* local = local_reservations + tid * num_process;
		WFEInfo** field = &(retired[tid].ui);
		WFEInfo* info = *field;
		if (info == nullptr) return;
		for (int i = 0; i < num_process; i++) {
			for (int j = 0; j < he_num; j++) {
				local[i].slot[j].pair[0].store(reservations[i].slot[j].pair[0].load(std::memory_order_acquire), std::memory_order_relaxed);
			}
		}
		for (int i = 0; i < num_process; i++) {
			local[i].slot[he_num].pair[0].store(reservations[i].slot[he_num].pair[0].load(std::memory_order_acquire), std::memory_order_relaxed);
		}
		size_t tot_reclaimed = 0;
		size_t tot_retired = 0;
		do {
			WFEInfo* curr = info;
			info = curr->next;
			tot_retired++;
			uint64_t ce = counter_end.ui.load(std::memory_order_acquire);
			if (can_delete(local, curr->birth_epoch, curr->retire_epoch, 0, he_num+1)) {
				uint64_t cs = counter_start.ui.load(std::memory_order_acquire);
				if (ce == cs || (can_delete(reservations, curr->birth_epoch, curr->retire_epoch, he_num+1, he_num+2) && can_delete(reservations, curr->birth_epoch, curr->retire_epoch, 0, he_num))) {
					*field = info;
	                // this->pool->add(tid, (T*)curr - 1);
            #ifdef DEAMORTIZE_FREE_CALLS
                threadData[tid].deamortizedFreeables->add((T*)curr - 1);
            #else
				this->pool->add(tid, (T*)curr - 1); //reclaim
            #endif

					tot_reclaimed++;
					continue;
				}
			}
			field = &curr->next;
		} while (info != nullptr);
        // COUTATOMICTID("before_sz= "<<tot_retired<<" after_sz= " << (tot_retired - tot_reclaimed) << " reclaimed=" << (tot_reclaimed) << std::endl);
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
    }
    void deinitThread(const int tid) {
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif

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

    reclaimer_wfe(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_wfe helping=" << this->shouldHelp() << std::endl;

		num_process = numProcesses;
        freq = 32000; //default in ibr benchamark 30;
        epoch_freq = 150;
        he_num = 3; // 3 for hmlist 2 shoudl work for harris and lazylist

		
		// use this->template to avoid error "there are no arguments to memalign that depend on a template parameter" 
		retired = new padded<WFEInfo*>[num_process];
		// reservations = (WFESlot*) memalign(alignof(WFESlot), sizeof(WFESlot) * num_process);
		reservations = (WFESlot*) std::aligned_alloc(alignof(WFESlot), sizeof(WFESlot) * num_process);

		// states = (WFEState*) memalign(alignof(WFEState), sizeof(WFEState) * num_process);
		states = (WFEState*) std::aligned_alloc(alignof(WFEState), sizeof(WFEState) * num_process);

		// local_reservations = (WFESlot*) memalign(alignof(WFESlot), sizeof(WFESlot) * num_process * num_process);
		local_reservations = (WFESlot*) std::aligned_alloc(alignof(WFESlot), sizeof(WFESlot) * num_process * num_process);

		for (int i = 0; i<num_process; i++){
			retired[i].ui = nullptr;
			for (int j = 0; j<he_num; j++){
				states[i].state[j].result.pair[0] = 0;
				states[i].state[j].result.pair[1] = 0;
				states[i].state[j].pointer = 0;
				states[i].state[j].epoch = 0;
				reservations[i].slot[j].pair[0] = 0;
				reservations[i].slot[j].pair[1] = 0;
			}
			reservations[i].slot[he_num].pair[0] = 0;
			reservations[i].slot[he_num].pair[1] = 0;
			reservations[i].slot[he_num+1].pair[0] = 0;
			reservations[i].slot[he_num+1].pair[1] = 0;
		}
		retire_counters = new padded<uint64_t>[num_process];
		alloc_counters = new padded<uint64_t>[num_process];
		counter_start.ui.store(0, std::memory_order_release);
		counter_end.ui.store(0, std::memory_order_release);
		epoch.ui.store(1, std::memory_order_release); // use 0 as infinity
    #ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = NULL;
    #endif		
    }
    ~reclaimer_wfe()
    {
        // std::cout <<"reclaimer destructor started" <<std::endl;
		COUTATOMIC("epoch_freq= " << epoch_freq << "empty_freq= " << freq <<std::endl);

        // std::cout <<"reclaimer destructor finished" <<std::endl;
    }
};

#endif //RECLAIM_WFE_H
