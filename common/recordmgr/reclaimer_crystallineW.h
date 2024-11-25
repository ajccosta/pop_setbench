/**
 * The code is adapted from https://github.com/rusnikola/wfsmr to make it work with Setbench.
 * Copyright (c) 2020 Ruslan Nikolaev.  All Rights Reserved.
 * 
 * This file implements CRYSTALLINE-W memory reclamation (https://drops.dagstuhl.de/opus/volltexte/2021/14862/pdf/LIPIcs-DISC-2021-60.pdf).
 * The exact file for CRYSTALLINE-W is WFRTracker.hpp (waitfree references) in the wfsmr code.
 * belongs to DAOI_RECLAIMERS bcse needs birth epoch in data (node) and read())
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */

#ifndef RECLAIM_CRYSTALLINE_W_H
#define RECLAIM_CRYSTALLINE_W_H

#include <atomic>
#include "ConcurrentPrimitives.h"
#include "dcas.h"

#define WFR_INVPTR64	((uint64_t)-1LL)
#define WFR_INVPTR		((WFRInfo*)-1LL)
#define MAX_WFR			8
#define MAX_WFRC		12
#define WFR_PROTECT1	(1ULL << 63)
#define WFR_PROTECT2	(1ULL << 62)

/* The reference node flag (batch_link). */
#define WFR_RNODE(n)	((WFRInfo *)((size_t)(n) ^ 0x1U))
#define WFR_IS_RNODE(n)	((size_t)(n) & 0x1U)


template <typename T = void, class Pool = pool_interface<T>>
class reclaimer_crystallineW : public reclaimer_interface<T, Pool>
{
private:
    int num_process;
    int freq;
    int epoch_freq;
	int num_hr;

public:
	struct WFRInfo;

	union word_pair_t {
		std::atomic<WFRInfo*> list[2];
		std::atomic<uint64_t> pair[2];
		alignas(16) std::atomic<__uint128_t> full;
	};

	union value_pair_t {
		WFRInfo* list[2];
		uint64_t pair[2];
		__uint128_t full;
	};

	struct state_t {
		word_pair_t result; // for helpee only
		std::atomic<uint64_t> epoch; // for helpee only
		std::atomic<uint64_t> pointer; // for helpee only
		std::atomic<WFRInfo*> parent;
		void* _pad;
	};

	struct WFRSlot;

	struct WFRInfo {
		union {
			std::atomic<WFRInfo*> next; // list nodes (inserted)
			word_pair_t* slot; // list nodes (preparation)
			uint64_t birth_epoch; // the refs node
		};
		std::atomic<WFRInfo*> batch_link;
		union {
			std::atomic<uintptr_t> refs; // the refs node
			WFRInfo* batch_next; // list nodes
		};
	};

	struct WFRBatch {
		WFRInfo* first;
		WFRInfo* last;
		size_t counter;
		size_t list_count;
		WFRInfo* list;
		alignas(128) char _pad[0];
	};

	struct WFRSlot {
		// do not reorder
		word_pair_t first[MAX_WFR];
		word_pair_t epoch[MAX_WFR];
		state_t state[MAX_WFR];
		alignas(128) char _pad[0];
	};

private:

	WFRSlot* slots;
	WFRBatch* batches;
	padded<uint64_t>* alloc_counters;
	paddedAtomic<uint64_t> epoch;
	paddedAtomic<uint64_t> slow_counter;

public:
    template <typename _Tp1>
    struct rebind
    {
        typedef reclaimer_crystallineW<_Tp1, Pool> other;
    };
    template <typename _Tp1, typename _Tp2>
    struct rebind2
    {
        typedef reclaimer_crystallineW<_Tp1, _Tp2> other;
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
		WFRInfo* first[MAX_WFR];
		for (int i = 0; i < num_hr; i++) {
			first[i] = slots[tid].first[i].list[0].exchange(WFR_INVPTR, std::memory_order_acq_rel);
		}
		for (int i = 0; i < num_hr; i++) {
			if (first[i] != WFR_INVPTR)
				traverse(&batches[tid].list, first[i]);
		}
		free_list(batches[tid].list);
		batches[tid].list = nullptr;
		batches[tid].list_count = 0;		
    }

    //for Crystaline since helpping is done beforre allocation call this before alocating api in DS in setbench. 
	//before you call this also be careful about initing the reclaimer related variables in allocated node from setbench allocator.
	// need to init birth epoch and an atomic pointer batch_link field pointer field in node.
	// inline void updateAllocCounterAndEpoch(const int tid)
    // {
	// 	alloc_counters[tid] = alloc_counters[tid]+1;
	// 	if (alloc_counters[tid] % epoch_freq == 0) {
	// 		// help other threads first
	// 		help_read(tid);
	// 		// only after that increment the counter
	// 		epoch.ui.fetch_add(1, std::memory_order_acq_rel);
	// 	}
    //     // COUTATOMICTID("epoch="<<epoch.load(std::memory_order_acquire)<<std::endl);
    // }

	T* instrumentedAlloc(const int tid)
	{
		alloc_counters[tid] = alloc_counters[tid]+1;
		if (alloc_counters[tid] % (epoch_freq) == 0){
			help_read(tid);
			epoch.ui.fetch_add(1, std::memory_order_acq_rel);
		}

		char* block = (char*) malloc(sizeof(WFRInfo) + sizeof(T));
		WFRInfo* info = (WFRInfo*) (block + sizeof(T));
		info->birth_epoch = getEpoch();
		info->batch_link.store(nullptr, std::memory_order_relaxed);
		return (T*) block;
	}		

	uint64_t getEpoch() {
		return epoch.ui.load(std::memory_order_acquire);
	}


	inline void help_thread(int tid, int index, int mytid)
	{
		value_pair_t last_result;
		last_result.full = dcas_load(slots[tid].state[index].result.full, std::memory_order_acquire);
		if (last_result.pair[0] != WFR_INVPTR64)
			return;
		uint64_t birth_epoch = slots[tid].state[index].epoch.load(std::memory_order_acquire);
		WFRInfo* parent = slots[tid].state[index].parent.load(std::memory_order_acquire);
		if (parent != nullptr) {
			slots[mytid].first[num_hr].list[0].store(nullptr, std::memory_order_seq_cst);
			slots[mytid].epoch[num_hr].pair[0].store(birth_epoch, std::memory_order_seq_cst);
		}
		slots[mytid].state[num_hr].parent.store(parent, std::memory_order_seq_cst);
		std::atomic<T*> *obj = (std::atomic<T*> *) slots[tid].state[index].pointer.load(std::memory_order_acquire);
		uint64_t seqno = slots[tid].epoch[index].pair[1].load(std::memory_order_acquire);
		if (last_result.pair[1] == seqno) {
			uint64_t prev_epoch = getEpoch();
			do {
				prev_epoch = do_update(prev_epoch, num_hr+1, mytid);
				T* ptr = obj ? obj->load(std::memory_order_acquire) : nullptr;
				uint64_t curr_epoch = getEpoch();
				if (curr_epoch == prev_epoch) {
					value_pair_t value;
					value.pair[0] = (uint64_t) ptr;
					value.pair[1] = curr_epoch;
					if (dcas_compare_exchange_strong(slots[tid].state[index].result.full, last_result.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire)) {
						// an empty epoch transition
						uint64_t expseqno = seqno;
						slots[tid].epoch[index].pair[1].compare_exchange_strong(expseqno, seqno + 1, std::memory_order_acq_rel, std::memory_order_relaxed);
						// clean up the list
						value.list[0] = nullptr;
						value.pair[1] = seqno + 1;
						value_pair_t old;
						old.pair[1] = slots[tid].first[index].pair[1].load(std::memory_order_acquire);
						old.list[0] = slots[tid].first[index].list[0].load(std::memory_order_acquire);
						while (old.pair[1] == seqno) { // n iterations at most
							if (dcas_compare_exchange_weak(slots[tid].first[index].full, old.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire)) {
								// clean up the list
								if (old.list[0] != WFR_INVPTR)
									traverse_cache(&batches[mytid], old.list[0]);
								break;
							}
						}
						seqno++;
						// set the real epoch
						value.pair[0] = curr_epoch;
						value.pair[1] = seqno + 1;
						old.pair[1] = slots[tid].epoch[index].pair[1].load(std::memory_order_acquire);
						old.pair[0] = slots[tid].epoch[index].pair[0].load(std::memory_order_acquire);
						while (old.pair[1] == seqno) { // 2 iterations at most
							if (dcas_compare_exchange_weak(slots[tid].epoch[index].full, old.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire)) {
								break;
							}
						}
						// check if the node is already retired
						// @J If I have batch_link as node variable then needn't do following pointer manipulations.
						uint64_t ptr_val = (uint64_t) ptr & 0xFFFFFFFFFFFFFFFCULL;
						WFRInfo* ptr_node = (WFRInfo*) (ptr_val + sizeof(T));
						
						if (ptr_val != 0 && ptr_node->batch_link.load(std::memory_order_acquire) != nullptr) {
							WFRInfo* refs = get_refs_node(ptr_node);
							refs->refs.fetch_add(1, std::memory_order_acq_rel);
							// clean up the list
							value.list[0] = WFR_RNODE(refs);
							value.pair[1] = seqno + 1;
							old.pair[1] = slots[tid].first[index].pair[1].load(std::memory_order_acquire);
							old.list[0] = slots[tid].first[index].list[0].load(std::memory_order_acquire);
							while (old.pair[1] == seqno) { // n iterations at most
								if (dcas_compare_exchange_weak(slots[tid].first[index].full, old.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire)) {
									// clean up the list
									if (old.list[0] != WFR_INVPTR)
										traverse_cache(&batches[mytid], old.list[0]);
									goto done;
								}
							}
							// already inserted
							refs->refs.fetch_sub(1, std::memory_order_acq_rel);
						} else {
							// an empty list transition
							uint64_t expseqno = seqno;
							slots[tid].first[index].pair[1].compare_exchange_strong(expseqno, seqno + 1, std::memory_order_acq_rel, std::memory_order_relaxed);
						}
					}
					break;
				}
				prev_epoch = curr_epoch;
			} while (last_result.full == dcas_load(slots[tid].state[index].result.full, std::memory_order_acquire));
			done:
			if (slots[mytid].epoch[num_hr+1].pair[0].exchange(0, std::memory_order_seq_cst) != 0) {
				WFRInfo* first = slots[mytid].first[num_hr+1].list[0].exchange(WFR_INVPTR, std::memory_order_acq_rel);
				traverse_cache(&batches[mytid], first);
			}
		}
		// the helpee provided an extra reference
		if (slots[mytid].state[num_hr].parent.exchange(nullptr, std::memory_order_seq_cst) != parent) {
			WFRInfo* refs = get_refs_node(parent);
			if (refs->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				refs->next = batches[mytid].list;
				batches[mytid].list = refs;
			}
		}
		// the parent reservation reference
		if (slots[mytid].epoch[num_hr].pair[0].exchange(0, std::memory_order_seq_cst) != 0) {
			WFRInfo* first = slots[mytid].first[num_hr].list[0].exchange(WFR_INVPTR, std::memory_order_acq_rel);
			traverse_cache(&batches[mytid], first);
		}
		free_list(batches[mytid].list);
		batches[mytid].list = nullptr;
		batches[mytid].list_count = 0;
	}

	inline WFRInfo* get_refs_node(WFRInfo* node)
	{
		WFRInfo* refs = node->batch_link.load(std::memory_order_acquire);
		if (WFR_IS_RNODE(refs)) refs = node;
		return refs;
	}

	__attribute__((noinline)) void help_read(int mytid)
	{
		// locate threads that need helping
		if (slow_counter.ui.load(std::memory_order_acquire) != 0) {
			for (int i = 0; i < num_process; i++) {
				for (int j = 0; j < num_hr; j++) {
					uint64_t result_ptr = slots[i].state[j].result.pair[0].load(std::memory_order_acquire);
					if (result_ptr == WFR_INVPTR64) {
						help_thread(i, j, mytid);
					}
				}
			}
		}
	}


	//internal func
	inline void free_list(WFRInfo* list) {
		while (list != nullptr) {
			WFRInfo* start = WFR_RNODE(list->batch_link.load(std::memory_order_relaxed));
			list = list->next;
			do {
				T* obj = (T*) start - 1;
				start = start->batch_next;

				this->pool->add(tid, obj);
			} while (start != nullptr);
		}
	}

	void traverse(WFRInfo** list, WFRInfo* next) {
		while (true) {
			WFRInfo* curr = next;
			if (!curr)
				break;
			if (WFR_IS_RNODE(curr)) {
				// A special case with a terminal refs node
				WFRInfo* refs = WFR_RNODE(curr);
				if (refs->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
					refs->next = *list;
					*list = refs;
				}
				break;
			}
			next = curr->next.exchange(WFR_INVPTR, std::memory_order_acq_rel);
			WFRInfo* refs = curr->batch_link.load(std::memory_order_relaxed);
			if (refs->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				refs->next = *list;
				*list = refs;
			}
		}
	}

	inline void traverse_cache(WFRBatch *batch, WFRInfo* next) {
		if (next != nullptr) {
			if (batch->list_count == MAX_WFRC) {
				free_list(batch->list);
				batch->list = nullptr;
				batch->list_count = 0;
			}
			traverse(&batch->list, next);
			batch->list_count++;
		}
	}

	__attribute__((noinline)) uint64_t do_update(uint64_t curr_epoch, int index, int tid) {
		// Dereference previous nodes
		if (slots[tid].first[index].list[0].load(std::memory_order_acquire) != nullptr) {
			WFRInfo* first = slots[tid].first[index].list[0].exchange(WFR_INVPTR, std::memory_order_acq_rel);
			if (first != WFR_INVPTR) traverse_cache(&batches[tid], first);
			slots[tid].first[index].list[0].store(nullptr, std::memory_order_seq_cst);
			curr_epoch = getEpoch();
		}
		slots[tid].epoch[index].pair[0].store(curr_epoch, std::memory_order_seq_cst);
		return curr_epoch;
	}

	// T* read(std::atomic<T*>& obj, int index, int tid, T* node) {
	T* readByPtrToTypeAndPtr(const int tid, int index, std::atomic<T*> &ptrToObj, T* obj) {
		// the fast path
		uint64_t prev_epoch = slots[tid].epoch[index].pair[0].load(std::memory_order_acquire);
		size_t attempts = 16;
		do {
			T* ptr = ptrToObj.load(std::memory_order_acquire);
			uint64_t curr_epoch = getEpoch();
			if (curr_epoch == prev_epoch){
				return ptr;
			} else {
				prev_epoch = do_update(curr_epoch, index, tid);
			}
		} while (--attempts != 0);

		return slow_path(&ptrToObj, index, tid, obj);
	}    

    // T* read(int tid, int idx, std::atomic<T*> &obj)
    // {
	// 	uint64_t prev_epoch = slots[tid].epoch[index].load(std::memory_order_acquire);
	// 	while (true) {
	// 		T* ptr = obj.load(std::memory_order_acquire);
	// 		uint64_t curr_epoch = getEpoch();
	// 		if (curr_epoch == prev_epoch){
	// 			return ptr;
	// 		} else {
	// 			prev_epoch = do_update(curr_epoch, index, tid);
	// 		}
	// 	}
    // }

    //not writing reserve_slot() from original implementation. I think I am not gonna use in any ds.
    // Might be wrong, in case I may need it for some DS in future. 

	__attribute__((noinline)) T* slow_path(std::atomic<T*>* obj, int index, int tid, T* node)
	{
		// get the birth epoch for the parent object
		uint64_t birth_epoch = 0;
		WFRInfo* parent = nullptr;
		if (node != nullptr) {
			parent = (WFRInfo *)(node + 1);
			birth_epoch = parent->birth_epoch;
			WFRInfo* info = parent->batch_link.load(std::memory_order_acquire);
			// already retired
			if (info != nullptr && !WFR_IS_RNODE(info))
				birth_epoch = info->birth_epoch;
		}
		// the slow path
		uint64_t prev_epoch = slots[tid].epoch[index].pair[0].load(std::memory_order_acquire);
		slow_counter.ui.fetch_add(1, std::memory_order_acq_rel);
		slots[tid].state[index].pointer.store((uint64_t) obj, std::memory_order_release);
		slots[tid].state[index].parent.store(parent, std::memory_order_release);
		slots[tid].state[index].epoch.store(birth_epoch, std::memory_order_release);
		uint64_t seqno = slots[tid].epoch[index].pair[1].load(std::memory_order_acquire);
		value_pair_t last_result;
		last_result.pair[0] = WFR_INVPTR64;
		last_result.pair[1] = seqno;
		dcas_store(slots[tid].state[index].result.full, last_result.full, std::memory_order_release);

		value_pair_t old, value;
		uint64_t result_epoch, result_ptr, expseqno;
		WFRInfo* first;
		do {
			T* ptr = obj ? obj->load(std::memory_order_acquire) : nullptr;
			uint64_t curr_epoch = getEpoch();
			if (curr_epoch == prev_epoch) {
				last_result.pair[0] = WFR_INVPTR64;
				last_result.pair[1] = seqno;
				value.pair[0] = 0;
				value.pair[1] = 0;
				if (dcas_compare_exchange_strong(slots[tid].state[index].result.full, last_result.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire)) {
					slots[tid].epoch[index].pair[1].store(seqno + 2, std::memory_order_release);
					slots[tid].first[index].pair[1].store(seqno + 2, std::memory_order_release);
					slow_counter.ui.fetch_sub(1, std::memory_order_acq_rel);
					return ptr;
				}
			}
			// Dereference previous nodes
			if (slots[tid].first[index].list[0].load(std::memory_order_acquire) != nullptr) {
				first = slots[tid].first[index].list[0].exchange(nullptr, std::memory_order_acq_rel);
				// the result is produced
				if (slots[tid].first[index].pair[1].load(std::memory_order_acquire) != seqno)
					goto done;
				if (first != WFR_INVPTR) traverse_cache(&batches[tid], first);
				curr_epoch = getEpoch();
			}
			first = nullptr;
			old.pair[0] = prev_epoch;
			old.pair[1] = seqno;
			value.pair[0] = curr_epoch;
			value.pair[1] = seqno;
			dcas_compare_exchange_strong(slots[tid].epoch[index].full, old.full, value.full, std::memory_order_seq_cst, std::memory_order_acquire);
			prev_epoch = curr_epoch;
			result_ptr = slots[tid].state[index].result.pair[0].load(std::memory_order_acquire);
		} while (result_ptr == WFR_INVPTR64);

		// an empty epoch transition
		expseqno = seqno;
		slots[tid].epoch[index].pair[1].compare_exchange_strong(expseqno, seqno + 1, std::memory_order_acq_rel, std::memory_order_relaxed);
		// clean up the list
		value.list[0] = nullptr;
		value.pair[1] = seqno + 1;
		old.pair[1] = slots[tid].first[index].pair[1].load(std::memory_order_acquire);
		old.list[0] = slots[tid].first[index].list[0].load(std::memory_order_acquire);
		while (old.pair[1] == seqno) { // n iterations at most
			if (dcas_compare_exchange_weak(slots[tid].first[index].full, old.full, value.full, std::memory_order_acq_rel, std::memory_order_acquire)) {
				// save the list
				if (old.list[0] != WFR_INVPTR)
					first = old.list[0];
				break;
			}
		}

		done:
		
		seqno++;

		// set the epoch
		slots[tid].epoch[index].pair[1].store(seqno + 1, std::memory_order_release);
		result_epoch = slots[tid].state[index].result.pair[1].load(std::memory_order_acquire);
		slots[tid].epoch[index].pair[0].store(result_epoch, std::memory_order_release);

		// check if the node is already retired
		slots[tid].first[index].pair[1].store(seqno + 1, std::memory_order_release);
		result_ptr = slots[tid].state[index].result.pair[0].load(std::memory_order_acquire) & 0xFFFFFFFFFFFFFFFCULL;
		WFRInfo* ptr_node = (WFRInfo*) (result_ptr + sizeof(T));
		if (result_ptr != 0 && ptr_node->batch_link.load(std::memory_order_acquire) != nullptr) {
			WFRInfo* refs = get_refs_node(ptr_node);
			refs->refs.fetch_add(1, std::memory_order_acq_rel);
			if (first != WFR_INVPTR) traverse_cache(&batches[tid], first);
			first = slots[tid].first[index].list[0].exchange(WFR_RNODE(refs), std::memory_order_acq_rel);
		}
		slow_counter.ui.fetch_sub(1, std::memory_order_acq_rel);

		// Traverse the removed list
		if (first != WFR_INVPTR) traverse_cache(&batches[tid], first);

		// Hand off the parent object if it's already retired
		if (parent != nullptr && parent->batch_link.load(std::memory_order_acquire) != nullptr) {
			WFRInfo* refs = get_refs_node(parent);
			refs->refs.fetch_add(WFR_PROTECT2, std::memory_order_acq_rel);
			size_t adjs = -WFR_PROTECT2;
			for (int i = 0; i < num_process; i++) {
				WFRInfo* exp = parent;
				if (slots[i].state[num_hr].parent.compare_exchange_strong(exp, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed)) {
					adjs++;
				}
			}
			refs->refs.fetch_add(adjs, std::memory_order_acq_rel);
		}

		return (T*) result_ptr;
	}




	void try_retire(WFRBatch* batch) {
		WFRInfo* curr = batch->first;
		WFRInfo* refs = batch->last;
		uint64_t min_epoch = refs->birth_epoch;
		// Find available slots
		WFRInfo* last = curr;
		for (int i = 0; i < num_process; i++) {
			int j = 0;
			for (; j < num_hr; j++) {
				WFRInfo* first = slots[i].first[j].list[0].load(std::memory_order_acquire);
				if (first == WFR_INVPTR)
					continue;
				if (slots[i].first[j].pair[1].load(std::memory_order_acquire) & 0x1U)
					continue; // in the slow-path final transition
				uint64_t epoch = slots[i].epoch[j].pair[0].load(std::memory_order_acquire);
				if (epoch < min_epoch)
					continue;
				if (slots[i].epoch[j].pair[1].load(std::memory_order_acquire) & 0x1U)
					continue; // in the slow-path final transition
				if (last == refs)
					return;
				last->slot = &slots[i].first[j];
				last = last->batch_next;
			}
			for (; j < num_hr + 2; j++) {
				WFRInfo* first = slots[i].first[j].list[0].load(std::memory_order_acquire);
				if (first == WFR_INVPTR)
					continue;
				uint64_t epoch = slots[i].epoch[j].pair[0].load(std::memory_order_acquire);
				if (epoch < min_epoch)
					continue;
				if (last == refs)
					return;
				last->slot = &slots[i].first[j];
				last = last->batch_next;
			}
		}
		// Retire if successful
		size_t adjs = -WFR_PROTECT1;
		for (; curr != last; curr = curr->batch_next) {
			word_pair_t* slot_first = curr->slot;
			word_pair_t* slot_epoch = (word_pair_t*) (slot_first + MAX_WFR);
			curr->next.store(nullptr, std::memory_order_relaxed);
			if (slot_first->list[0].load(std::memory_order_acquire) == WFR_INVPTR)
				continue;
			uint64_t epoch = slot_epoch->pair[0].load(std::memory_order_acquire);
			if (epoch < min_epoch)
				continue;
			WFRInfo* prev = slot_first->list[0].exchange(curr, std::memory_order_acq_rel);
			if (prev != nullptr) {
				if (prev == WFR_INVPTR) {
					// Transitioning
					WFRInfo* exp = curr;
					if (slot_first->list[0].compare_exchange_strong(exp, WFR_INVPTR, std::memory_order_acq_rel, std::memory_order_relaxed)) {
						continue;
					}
				} else {
					// The list was already tainted
					WFRInfo* exp = nullptr;
					if (!curr->next.compare_exchange_strong(exp, prev, std::memory_order_acq_rel, std::memory_order_relaxed)) {
						WFRInfo* list = nullptr;
						traverse(&list, prev);
						free_list(list);
					}
				}
			}
			adjs++;
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

	void retire(const int tid, T* obj) {
		if (obj == nullptr) { return; }
		WFRInfo* node = (WFRInfo *) (obj + 1);
		if (!batches[tid].first) {
			batches[tid].last = node;
			node->refs.store(WFR_PROTECT1, std::memory_order_relaxed);
		} else {
			// Use the refs node to keep the minimum birth era
			if (batches[tid].last->birth_epoch > node->birth_epoch)
				batches[tid].last->birth_epoch = node->birth_epoch;
			node->batch_link.store(batches[tid].last, std::memory_order_seq_cst);
			node->batch_next = batches[tid].first;
		}

		batches[tid].first = node;
		batches[tid].counter++;
		if (batches[tid].counter % freq == 0) {
			batches[tid].last->batch_link.store(WFR_RNODE(node), std::memory_order_seq_cst);
			size_t before_sz = batches[tid].counter;
			try_retire(&batches[tid]);
			size_t after_sz = batches[tid].counter;
			// COUTATOMICTID("before_sz= "<<before_sz<<" after_sz= " << after_sz << " reclaimed=" << (before_sz - after_sz) << std::endl);
		}
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

    reclaimer_crystallineW(const int numProcesses, Pool *_pool, debugInfo *const _debug, RecoveryMgr<void *> *const _recoveryMgr = NULL)
        : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr)
    {
        VERBOSE std::cout << "constructor reclaimer_crystallineW helping=" << this->shouldHelp() << std::endl;
		num_process = numProcesses;
        freq = 30; //30;
        epoch_freq = 150;
        num_hr = 3; // 3 for hmlist 2 shoudl work for harris and lazylist

        // use this->template to avoid error "there are no arguments to memalign that depend on a template parameter" 
		// batches = (WFRBatch*) this->template memalign(alignof(WFRBatch), sizeof(WFRBatch) * num_process);
		batches = (WFRBatch*) std::aligned_alloc(alignof(WFRBatch), sizeof(WFRBatch) * num_process);

		// slots = (WFRSlot*) this->template memalign(alignof(WFRSlot), sizeof(WFRSlot) * num_process);
		slots = (WFRSlot*)  std::aligned_alloc(alignof(WFRSlot), sizeof(WFRSlot) * num_process);

		alloc_counters = new padded<uint64_t>[num_process];
		for (int i = 0; i < num_process; i++) {
			alloc_counters[i].ui = 0;
			batches[i].first = nullptr;
			batches[i].counter = 0;
			batches[i].list_count = 0;
			batches[i].list = nullptr;
			for (int j = 0; j < num_hr+2; j++) {
				slots[i].first[j].list[0].store(WFR_INVPTR, std::memory_order_release);
				slots[i].first[j].pair[1].store(0, std::memory_order_release);
				slots[i].epoch[j].pair[0].store(0, std::memory_order_release);
				slots[i].epoch[j].pair[1].store(0, std::memory_order_release);
				slots[i].state[j].result.pair[0] = 0;
				slots[i].state[j].result.pair[1] = 0;
				slots[i].state[j].pointer = 0;
				slots[i].state[j].parent = nullptr;
				slots[i].state[j].epoch = 0;
			}
		}
		slow_counter.ui.store(0, std::memory_order_release);
		epoch.ui.store(1, std::memory_order_release);
    }
    ~reclaimer_crystallineW()
    {
        // std::cout <<"reclaimer destructor started" <<std::endl;

        // std::cout <<"reclaimer destructor finished" <<std::endl;
    }
};

#endif //RECLAIM_CRYSTALLINE_W_H
