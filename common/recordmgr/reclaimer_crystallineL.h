/**
 * The code is adapted from https://github.com/rusnikola/wfsmr to make it work with Setbench.
 * Copyright (c) 2020 Ruslan Nikolaev.  All Rights Reserved.
 * 
 * This file implements CRYSTALLINE-L memory reclamation (https://drops.dagstuhl.de/opus/volltexte/2021/14862/pdf/LIPIcs-DISC-2021-60.pdf).
 * The exact file for CRYSTALLINE-L is HRTracker.hpp in the wfsmr code.
 * belongs to DAOI_RECLAIMERS bcse needs birth epoch in data (node) and read())
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

#ifndef RECLAIM_CRYSTALLINE_L_H
#define RECLAIM_CRYSTALLINE_L_H

#include <atomic>
#include "ConcurrentPrimitives.h"

#define HR_INVPTR	((HRInfo*)-1LL)
#define MAX_HR		16
#define MAX_HRC		12

template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_crystallineL : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int freq;
    int epoch_freq;
	int num_hr;

public:
	struct HRSlot;

	struct HRInfo {
		union {
			struct {
				union {
					std::atomic<HRInfo*> next;
					std::atomic<HRInfo*>* slot;
				};
				HRInfo* batch_link;
				union {
					std::atomic<uintptr_t> refs;
					HRInfo* batch_next;
				};
			};
			uint64_t birth_epoch;
		};
	};

	struct HRBatch {
		uint64_t min_epoch;
		HRInfo* first;
		HRInfo* last;
		size_t counter;
		size_t list_count;
		HRInfo* list;
		alignas(128) char pad[0];
	};

	struct HRSlot {
		// do not reorder
		std::atomic<HRInfo*> first[MAX_HR];
		std::atomic<uint64_t> epoch[MAX_HR];
		alignas(128) char pad[0];
	};

private:

	HRSlot* slots;
	HRBatch* batches;
	padded<uint64_t>* alloc_counters;
	paddedAtomic<uint64_t> epoch;

public:
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_crystallineL<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_crystallineL<_Tp1, _Tp2> other;
    };

    template <typename First, typename... Rest>
    inline bool startOp(const int tid, void *const *const reclaimers, const int numReclaimers, const bool readOnly = false)
    {
        bool result = true;

        return result;
    }

    //clear_all() in original implementation  
    inline void endOp(const int tid)
    {
		HRInfo* first[MAX_HR];
		for (int i = 0; i < num_hr; i++) {
			first[i] = slots[tid].first[i].exchange(HR_INVPTR, std::memory_order_acq_rel);
		}
		for (int i = 0; i < num_hr; i++) {
			if (first[i] != HR_INVPTR)
				traverse(&batches[tid].list, first[i]);
		}
		free_list(batches[tid].list);
		batches[tid].list = nullptr;
		batches[tid].list_count = 0;
    }

	//before you call this also be careful about initing the reclaimer related variables in allocated node from setbench allocator.
    // inline void updateAllocCounterAndEpoch(const int tid)
    // {
	// 	alloc_counters[tid] = alloc_counters[tid]+1;
	// 	if (alloc_counters[tid] % epoch_freq == 0){
	// 		epoch.ui.fetch_add(1, std::memory_order_acq_rel);
	// 	}
    //     // COUTATOMICTID("epoch="<<epoch.load(std::memory_order_acquire)<<std::endl);
    // }

	// work of updateAllocCounter done in this func now.
	T* instrumentedAlloc(const int tid)
	{
		alloc_counters[tid] = alloc_counters[tid]+1;
		if (alloc_counters[tid] % epoch_freq == 0){
			epoch.ui.fetch_add(1, std::memory_order_acq_rel);
		}

		char* block = (char*) malloc(sizeof(HRInfo) + sizeof(T));
		HRInfo* info = (HRInfo*) (block + sizeof(T));
		info->birth_epoch = getEpoch();
		return (T*) block;		
	}

	uint64_t getEpoch() {
		return epoch.ui.load(std::memory_order_acquire);
	}

	//internal func
    inline void free_list(HRInfo* list) {
		while (list != nullptr) 
        {
			HRInfo* start = list->batch_link;
			list = list->next;
			do {
				T* obj = (T*) start - 1;
				start = start->batch_next;

                this->pool->add(tid, obj);
			} while (start != nullptr);
		}
	}

	void traverse(HRInfo** list, HRInfo* next) {
		while (true) {
			HRInfo* curr = next;
			if (!curr)
				break;
			next = curr->next.load(std::memory_order_acquire);
			HRInfo* refs = curr->batch_link;
			if (refs->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				refs->next = *list;
				*list = refs;
			}
		}
	}

	inline void traverse_cache(HRBatch* batch, HRInfo* next) {
		if (next != nullptr) {
			if (batch->list_count == MAX_HRC) {
				free_list(batch->list);
				batch->list = nullptr;
				batch->list_count = 0;
			}
			batch->list_count++;
			traverse(&batch->list, next);
		}
	}

	__attribute__((noinline)) uint64_t do_update(uint64_t curr_epoch, int index, int tid) {
		// Dereference previous nodes
		if (slots[tid].first[index].load(std::memory_order_acquire) != nullptr) {
			HRInfo* first = slots[tid].first[index].exchange(HR_INVPTR, std::memory_order_acq_rel);
			if (first != HR_INVPTR) traverse_cache(&batches[tid], first);
			slots[tid].first[index].store(nullptr, std::memory_order_seq_cst);
			curr_epoch = getEpoch();
		}
		slots[tid].epoch[index].store(curr_epoch, std::memory_order_seq_cst);
		return curr_epoch;
	}    

    T* read(int tid, int index, std::atomic<T*> &obj)
    {
		uint64_t prev_epoch = slots[tid].epoch[index].load(std::memory_order_acquire);
		while (true) {
			T* ptr = obj.load(std::memory_order_acquire);
			uint64_t curr_epoch = getEpoch();
			if (curr_epoch == prev_epoch){
				// assert(ptr);
				return ptr;
			} else {
				prev_epoch = do_update(curr_epoch, index, tid);
			}
		}
    }

	// adding to debug why crystL doesnt work with HarrisList.
	void reserve_slot(int tid, int index, T* node) {
		uint64_t prev_epoch = slots[tid].epoch[index].load(std::memory_order_acquire);
		while (true) {
			uint64_t curr_epoch = getEpoch();
			if (curr_epoch == prev_epoch){
				return;
			} else {
				prev_epoch = do_update(curr_epoch, index, tid);
			}
		}
	}

    //not writing reserve_slot() from original implementation. I think I am not gonna use in any ds.
    // Might be wrong, in case I may need it for some DS in future. 


	void try_retire(HRBatch* batch) {
		HRInfo* curr = batch->first;
		HRInfo* refs = batch->last;
		uint64_t min_epoch = batch->min_epoch;
		// Find available slots
		HRInfo* last = curr;
		for (int i = 0; i < num_process; i++) {
			for (int j = 0; j < num_hr; j++) {
				HRInfo* first = slots[i].first[j].load(std::memory_order_acquire);
				if (first == HR_INVPTR)
					continue;
				uint64_t epoch = slots[i].epoch[j].load(std::memory_order_acquire);
				if (epoch < min_epoch)
					continue;
				if (last == refs)
					return;
				last->slot = &slots[i].first[j];
				last = last->batch_next;
			}
		}
		// Retire if successful
		size_t adjs = 0;
		for (; curr != last; curr = curr->batch_next) {
			std::atomic<HRInfo*>* slot_first = curr->slot;
			std::atomic<uint64_t>* slot_epoch = (std::atomic<uint64_t>*) (slot_first + MAX_HR);
			HRInfo* prev = slot_first->load(std::memory_order_acquire);
			do {
				if (prev == HR_INVPTR)
					goto next;
				uint64_t epoch = slot_epoch->load(std::memory_order_acquire);
				if (epoch < min_epoch)
					goto next;
				curr->next.store(prev, std::memory_order_relaxed);
			} while (!slot_first->compare_exchange_weak(prev, curr, std::memory_order_acq_rel, std::memory_order_acquire));
			adjs++;
            
            next: ;
		}
		// Adjust the reference count
		if (refs->refs.fetch_add(adjs, std::memory_order_acq_rel) == -adjs) {
			refs->next = nullptr;
			free_list(refs);
		}
		// Reset the batch
		batch->first = nullptr;
		batch->counter = 0;
	}

    // for all schemes except reference counting
	// obj is ds node type and contains birth epoch inited when node was allocated
    inline void retire(const int tid, T *obj)
    {
		if (obj == nullptr) { return; }
		
        HRInfo* node = (HRInfo *) (obj + 1);
		
        if (!batches[tid].first) {
			batches[tid].min_epoch = node->birth_epoch;
			batches[tid].last = node;
		} 
        else
        {
			if (batches[tid].min_epoch > node->birth_epoch)
				batches[tid].min_epoch = node->birth_epoch;
			node->batch_link = batches[tid].last;
		}

		// Implicitly initialize refs to 0 for the last node
		node->batch_next = batches[tid].first;

		batches[tid].first = node;
		batches[tid].counter++;
		if (batches[tid].counter % freq == 0)
        {
			
			batches[tid].last->batch_link = node;
			size_t before_sz = batches[tid].counter;
			try_retire(&batches[tid]);
			size_t after_sz = batches[tid].counter;
			// COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);

			// if ((before_sz - after_sz))
			// {
			// 	COUTATOMICTID("skipped= "<< (before_sz - after_sz)<<std::endl);
			// }
		}
    }

    void empty(const int tid)
    {
        // // erase safe objects
        // std::list<HeInfo> *myTrash = &(retired[tid].ui);

        // uint before_sz = myTrash->size();
        // // COUTATOMICTID("decided to empty! bag size=" << myTrash->size() << std::endl);

        // for (auto iterator = myTrash->begin(), end = myTrash->end(); iterator != end;)
        // {
        //     HeInfo res = *iterator;
    
        //     if (isFreeable(res.birth_epoch, res.retire_epoch))
        //     {
        //         this->pool->add(tid, res.obj);
        //         iterator = myTrash->erase(iterator); //return iterator corresponding to next of last erased item
        //     }
        //     else
        //     {
        //         ++iterator;
        //     }
        // }

        // uint after_sz = myTrash->size();
        // TRACE COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);
    }

    void debugPrintStatus(const int tid)
    {
    }

    //dummy declaration
    void initThread(const int tid) {}
    void deinitThread(const int tid) {}
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

    reclaimer_crystallineL(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_crystallineL helping=" << this->shouldHelp() << std::endl;

        num_process = numProcesses;
        freq = 30; //30;
        epoch_freq = 150;
        num_hr = 14; // 3 for hmlist 2 shoudl work for harris and lazylist

        // use this->template to avoid error "there are no arguments to memalign that depend on a template parameter" 
		batches = (HRBatch*)  std::aligned_alloc(alignof(HRBatch), sizeof(HRBatch) * num_process);
		// batches = (HRBatch*)  memalign(alignof(HRBatch), sizeof(HRBatch) * num_process);
		slots = (HRSlot*) std::aligned_alloc(alignof(HRSlot), sizeof(HRSlot) * num_process);
		// slots = (HRSlot*) memalign(alignof(HRSlot), sizeof(HRSlot) * num_process);

		alloc_counters = new padded<uint64_t>[num_process];
		for (int i = 0; i<num_process; i++) {
			alloc_counters[i].ui = 0;
			batches[i].first = nullptr;
			batches[i].counter = 0;
			batches[i].list_count = 0;
			batches[i].list = nullptr;
			for (int j = 0; j<num_hr; j++){
				slots[i].first[j].store(HR_INVPTR, std::memory_order_release);
				slots[i].epoch[j].store(0, std::memory_order_release);
			}
		}
		epoch.ui.store(1, std::memory_order_release);
    }
    ~reclaimer_crystallineL()
    {
        // std::cout <<"reclaimer destructor started" <<std::endl;

        // std::cout <<"reclaimer destructor finished" <<std::endl;
    }
};

#endif //RECLAIM_CRYSTALLINE_L_H
