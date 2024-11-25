/**
 * The code has shamelessly been copied from https://github.com/urcs-sync/Interval-Based-Reclamation to make Interval Based memory reclamation work with Setbench benchmark to 
 * compare Interval based reclamation algorithms and their accompanying memory reclamtion algorithms with NBR and DEBRA.
 * Please refer to original Interval Based Memory Reclamation paper, PPOPP 2018.
 * Ajay Singh (@J)
 * Multicore lab uwaterloo
 */



#ifndef CONCURRENT_PRIMITIVES_HPP
#define CONCURRENT_PRIMITIVES_HPP

#include <assert.h>
#include <stddef.h>
#include <iostream>
#include <atomic>
#include <string>


#include <sys/syscall.h>
#include <linux/membarrier.h>
static int
membarrier(int cmd, unsigned int flags, int cpu_id)
{
    return syscall(__NR_membarrier, cmd, flags, cpu_id);
}

static int
init_membarrier(void)
{
    int ret;

    /* Check that membarrier() is supported. */

    ret = membarrier(MEMBARRIER_CMD_QUERY, 0, 0);
    if (ret < 0) {
        perror("membarrier");
        return -1;
    }

    if (!(ret & MEMBARRIER_CMD_GLOBAL)) {
        fprintf(stderr,
            "membarrier does not support MEMBARRIER_CMD_GLOBAL\n");
        return -1;
    }

    return 0;
}


// #ifndef LEVEL1_DCACHE_LINESIZE
// #define LEVEL1_DCACHE_LINESIZE 128
// #endif

// #define CACHE_LINE_SIZE LEVEL1_DCACHE_LINESIZE

#define CACHE_LINE_SIZE BYTES_IN_CACHE_LINE

// Possibly helpful concurrent data structure primitives

// Pads data to cacheline size to eliminate false sharing



template<typename T>
class padded {
public:
   //[[ align(CACHE_LINE_SIZE) ]] T ui;	
	T ui;
private:
   /*uint8_t pad[ CACHE_LINE_SIZE > sizeof(T)
        ? CACHE_LINE_SIZE - sizeof(T)
        : 1 ];*/
	uint8_t pad[ 0 != sizeof(T)%CACHE_LINE_SIZE
        ?  CACHE_LINE_SIZE - (sizeof(T)%CACHE_LINE_SIZE)
        : CACHE_LINE_SIZE ];
public:
  padded<T> ():ui() {};
  // conversion from T (constructor):
  padded<T> (const T& val):ui(val) {};
  // conversion from A (assignment):
  padded<T>& operator= (const T& val) {ui = val; return *this;}
  // conversion to A (type-cast operator)
  operator T() {return T(ui);}
};//__attribute__(( aligned(CACHE_LINE_SIZE) )); // alignment confuses valgrind by shifting bits


template<typename T>
class paddedAtomic {
public:
   //[[ align(CACHE_LINE_SIZE) ]] T ui;	
	std::atomic<T> ui;
private:
	uint8_t pad[ 0 != sizeof(T)%CACHE_LINE_SIZE
        ?  CACHE_LINE_SIZE - (sizeof(T)%CACHE_LINE_SIZE)
        : CACHE_LINE_SIZE ];
public:
  paddedAtomic<T> ():ui() {}
  // conversion from T (constructor):
  paddedAtomic<T> (const T& val):ui(val) {}
  // conversion from A (assignment):
  paddedAtomic<T>& operator= (const T& val) {ui.store(val); return *this;}
  // conversion to A (type-cast operator)
  operator T() {return T(ui.load());}
};//__attribute__(( aligned(CACHE_LINE_SIZE) )); // alignment confuses valgrind by shifting bits



template<typename T>
class volatile_padded {
public:
   //[[ align(CACHE_LINE_SIZE) ]] volatile T ui;	
	volatile T ui;
private:
   uint8_t pad[ CACHE_LINE_SIZE > sizeof(T)
        ? CACHE_LINE_SIZE - sizeof(T)
        : 1 ];
public:
  volatile_padded<T> ():ui() {}
  // conversion from T (constructor):
  volatile_padded<T> (const T& val):ui(val) {}
  // conversion from T (assignment):
  volatile_padded<T>& operator= (const T& val) {ui = val; return *this;}
  // conversion to T (type-cast operator)
  operator T() {return T(ui);}
}__attribute__(( aligned(CACHE_LINE_SIZE) ));



// Counted pointer, used to eliminate ABA problem
template <class T>
class cptr;

// Counted pointer, local copy.  Non atomic, for use
// to create values for counted pointers.
template <class T>
class cptr_local{

	uint64_t ui
		__attribute__(( aligned(8) )) =0;

public:
	void init(const T* ptr, const uint32_t sn){
		uint64_t a;
		a = 0;
		a = (uint32_t)ptr;
		a = a<<32;
		a += sn;
		ui=a;
	}
	void init(const uint64_t initer){
		ui=initer;
	}
	void init(const cptr<T> ptr){
		ui=ptr.all();
	}
	void init(const cptr_local<T> ptr){
		ui=ptr.all();
	}
	uint64_t all() const{
		return ui;
	}

	T operator *(){return *this->ptr();}
	T* operator ->(){return this->ptr();}

	// conversion from T (constructor):
	cptr_local<T> (const T*& val) {init(val,0);}
	// conversion to T (type-cast operator)
	operator T*() {return this->ptr();}

	void storeNull(){
		ui=0;
	}


	T* ptr(){return (T*)((ui&0xffffffff00000000) >>32);}
	uint32_t sn(){return (ui&0x00000000ffffffff);}

	cptr_local<T>(){
		init(NULL,0);
	}
	cptr_local<T>(const uint64_t initer){
		init(initer);
	}
	cptr_local<T>(const T* ptr, const uint32_t sn){
		init(ptr,sn);
	}
	cptr_local<T>(const cptr_local<T> &cp){
		init(cp.all());
	}

	cptr_local<T> (const cptr<T>& cp) {init(cp.all());}
	// conversion from A (assignment):
	cptr_local<T>& operator= (const cptr<T>& cp) {init(cp.all()); return *this;}
};

// Counted pointer
template <class T>
class cptr{

	std::atomic<uint64_t> ui
		__attribute__(( aligned(8) ));

public:
	void init(const T* ptr, const uint32_t sn){
		uint64_t a;
		a = 0;
		a = (uint32_t)ptr;
		a = a<<32;
		a += sn;
		ui.store(a,std::memory_order::memory_order_release);
	}
	void init(const uint64_t initer){
		ui.store(initer);
	}
	T operator *(){return *this->ptr();}
	T* operator ->(){return this->ptr();}

  // conversion from T (constructor):
  cptr<T> (const T*& val) {init(val,0);}
  // conversion to T (type-cast operator)
  operator T*() {return this->ptr();}

	T* ptr(){return (T*)(((ui.load(std::memory_order::memory_order_consume))&0xffffffff00000000) >>32);}
	uint32_t sn(){return ((ui.load(std::memory_order::memory_order_consume))&0x00000000ffffffff);}

	uint64_t all() const{
		return ui;
	}	

	bool CAS(cptr_local<T> &oldval,T* newval){
		cptr_local<T> replacement;
		replacement.init(newval,oldval.sn()+1);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_release);
	}
	bool CAS(cptr_local<T> &oldval,cptr_local<T> &newval){
		cptr_local<T> replacement;
		replacement.init(newval.ptr(),oldval.sn()+1);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_release);
	}
	bool CAS(cptr<T> &oldval,T* newval){
		cptr_local<T> replacement;
		replacement.init(newval,oldval.sn()+1);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_release);
	}
	bool CAS(cptr<T> &oldval,cptr_local<T> &newval){
		cptr_local<T> replacement;
		replacement.init(newval.ptr(),oldval.sn()+1);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_release);
	}

	bool CAS(cptr_local<T> &oldval,T* newval, uint32_t newSn){
		cptr_local<T> replacement;
		replacement.init(newval,newSn);
		uint64_t old= oldval.all();
		return ui.compare_exchange_strong(old,replacement.all(),std::memory_order::memory_order_release);
	}

	void storeNull(){
		init(NULL,0);
	}

	void storePtr(T* newval){
		cptr_local<T> oldval;
		while(true){
			oldval.init(all());
			if(CAS(oldval,newval)){break;}
		};
	}

	cptr<T>(){
		init(NULL,0);
	}
	cptr<T>(const cptr<T>& cp){
		init(cp.all());
	}
	cptr<T>(const cptr_local<T>& cp){
		init(cp.all());
	}
	cptr<T>(const uint64_t initer){
		init(initer);
	}
	cptr<T>(const T* ptr, const uint32_t sn){
		init(ptr,sn);
	}

	/*bool operator==(cptr<T> &other){
		return other.ui==this->ui;
	}*/
};

// OLD CODE
/*template <typename T> struct padded_data{
public:
	T ui;
	bool operator==(const struct padded_data<T>  &x)
	{
		return ui==x.ui;
	}

 	operator T(void) const{
		return ui;
	}

private:
	//pad to cache line size
	uint8_t pad[LEVEL1_DCACHE_LINESIZE-sizeof(T)];

};
// Pads data to cacheline size to eliminate false sharing (but volatile)
template <typename T> struct volatile_padded_data{
public:
	volatile T ui;
	bool operator==(const T  &x)
	{
		//return x.closed == closed && x.t == t;
		return ui==x.ui;
	}

private:
	//pad to cache line size
	uint8_t pad[LEVEL1_DCACHE_LINESIZE-sizeof(T)];

};*/




#endif
