GPP = g++
### if you do not want PAPI to be used, invoke make with "has_libpapi=0"

has_libpapi=$(shell ldconfig -p | grep libpapi | wc -l)
has_libpapi=0
$(info has_libpapi is $(has_libpapi))
ifneq ($(has_libpapi), 0)
    FLAGS += -DUSE_PAPI
    LDFLAGS += -lpapi
endif

### if you do not want libnuma to be used, invoke make with "has_libnuma=0"
has_libnuma=$(shell ldconfig -p | grep libnuma | wc -l)
ifneq ($(has_libnuma), 0)
    FLAGS += -DUSE_LIBNUMA
    LDFLAGS += -lnuma
endif

use_asan=0
ifneq ($(use_asan), 0)
    LDFLAGS += -fsanitize=address -static-libasan
endif

use_asserts=0
ifeq ($(use_asserts), 0)
    FLAGS += -DNDEBUG
endif

use_fopenmp=1
ifeq ($(use_fopenmp), 1)
    FLAGS += -fopenmp
endif

use_timelines=0
ifeq ($(use_timelines), 1)
    $(info use_timelines is $(use_timelines))
    FLAGS += -DMEASURE_TIMELINE_STATS
endif

#effecient timeline stat collection
use_timeline_gstats=0
ifeq ($(use_timeline_gstats), 1)
    $(info use_timeline_gstats is $(use_timeline_gstats))
    FLAGS += -DMEASURE_TIMELINE_GSTATS
endif

no_optimize=0
ifeq ($(no_optimize), 1)
    FLAGS += -O0 -g
else
    FLAGS += -O3
endif

# Check if CPU_FREQ is set
ifeq ($(origin CPU_FREQ_GHZ), undefined)
  $(error CPU_FREQ_GHZ not defined. Usage: make CPU_FREQ=XXXX)
endif

FLAGS += -DCPU_FREQ_GHZ=$(CPU_FREQ_GHZ)

FLAGS += -DMAX_THREADS_POW2=512
# FLAGS += -DCPU_FREQ_GHZ=2.1 #$(shell ./experiments/get_cpu_ghz.sh)
#FLAGS += -DCPU_FREQ_GHZ=$(shell ./experiments/get_cpu_ghz.sh)
FLAGS += -DMEMORY_STATS=if\(1\) -DMEMORY_STATS2=if\(0\)
FLAGS += -std=c++14 -g -mcx16 -fconcepts # to avoid auto in parameter declaration warning #-gdwarf
# FLAGS += -DLLISTDS
FLAGS += -fno-omit-frame-pointer
# FLAGS += -DCALL_DEBUG_GC
#FLAGS += -DNO_CLEANUP_AFTER_WORKLOAD ### avoid executing data structure destructors, to save teardown time at the end of each trial (useful with massive trees)
#FLAGS += -DRAPID_RECLAMATION
FLAGS += -DPREFILL_INSERTION_ONLY
#FLAGS += -DMEASURE_REBUILDING_TIME
# FLAGS += -DMEASURE_TIMELINE_STATS
# FLAGS += -DMEASURE_TIMELINE_GSTATS # for allowing saving of timeline stats instead of printing. Saves logging overhead during experiment. 
FLAGS += -DUSE_TREE_STATS 
# FLAGS += -DUSE_DEBUGCOUNTERS
#FLAGS += -DOVERRIDE_PRINT_STATS_ON_ERROR
#FLAGS += -Wno-format
# FLAGS += -DDEAMORTIZE_ADAPTIVELY
# FLAGS += -DDEAMORTIZE_FREE_CALLS
# FLAGS += -DDEBRA_ORIGINAL_FREE
# FLAGS += -DDEAMORTIZE_PARTIAL  # for enabling execution of free calls after a certain number of ops.
# FLAGS += -DPERIODIC_THROUGHPUT_PRINT # for printing throughput periodically (1s interval) by main thread costly as involves accumulation of tids stats during experiment.
# FLAGS += -DPERIODIC_PT_THROUGHPUT_PRINT_EFFICIENT #for printing throughput periodically (1s interval). All thread GSTAT the throughput to prep data for data wrangling.
# FLAGS += -DMEASURE_TG_RECLAIM_RETIREDBAG
# FLAGS += -DMEASURE_TG_RECLAIM_HI_RETIREDBAG
# FLAGS += -DMEASURE_TG_RECLAIM_LO_RETIREDBAG
# FLAGS += -DMEASURE_TG_SIGNAL_ALL_EVENT
# FLAGS += -DMEASURE_TG_NUM_RECLAIMED_LO_EVENT
# FLAGS += -DALLOCATOR_BOTTLENECK_TEST # used only for none
FLAGS += $(xargs)

LDFLAGS += -L../lib
LDFLAGS += -I./ -I../ `find ../common -type d | sed s/^/-I/`
LDFLAGS += -lpthread
LDFLAGS += -ldl
LDFLAGS += -mrtm

bin_dir=bin
dir_guard:
	@mkdir -p $(bin_dir)

MINIMAL_BINS=$(patsubst ../ds/%/minimal_example.cpp,%.minimal,$(wildcard ../ds/*/minimal_example.cpp))
minimal_%: dir_guard
	$(GPP) $(FLAGS) ../ds/$(@:minimal_%=%)/minimal_example.cpp -o $(bin_dir)/$@ $(LDFLAGS)

# DATA_STRUCTURES=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/*/adapter.h))
DATA_STRUCTURES=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/hmlist/adapter.h))
DATA_STRUCTURES+=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/hm_hashtable/adapter.h))
# DATA_STRUCTURES+=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/harris*/adapter.h))
DATA_STRUCTURES+=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/brown_ext_ab*/adapter.h))
DATA_STRUCTURES+=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/herlihy_lazy*/adapter.h))
DATA_STRUCTURES+=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/guerr*/adapter.h))
POOLS=none
ALLOCATORS=new

#### legacy reclaimer build begin
RECLAIMERS= #none #nbr nbrplus none  # build the legacy or new specific reclaimers but not both together.

define make-custom-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4)_legacy.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4)_legacy.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS)
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4)_legacy.out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-custom-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

#### legacy reclaimer build end

#different category of relcaimers to decide which ds implementation to run
TOKEN_RECLAIMERS = #token1
TOKEN4_RECLAIMERS = #token4

OOI_RECLAIMERS = none #debra none #qsbr rcu #OPN_ONLY_INSTRUMENTATION_RECLAIMERS
OOI_IBR_RECLAIMERS = ibr_rcu # qsbr ibr_rcu #OPN_ONLY_INSTRUMENTATION_RECLAIMERS which require incrementing of alloc counters whenever a new node is allocated. NOTE OOI reclaimers use same ds file.

NZB_RECLAIMERS = nbr nbrplus #nbrplus #nbr nbrplus #nbr_orig nbr nbrplus #NEUTRALIZATION_BASED_RECLAIMERS

OOI_POP_RECLAIMERS = rcu_pop #rcu_pop rcu_popplus reclaimer with data node instrumented with be re, operation overhead no per read overhead. Uses Neutralization to publish reservations when needed.
HP_RECLAIMERS = #hazardptr 
IBR_HP_RECLAIMERS = ibr_hp ibr_hpasyf
IBR_HP_POP_RECLAIMERS = ibr_pophp ibr_popplushp
IBR_RCU_HP_POP_RECLAIMERS = rcu_pophp rcu_popplushp
DAOI_RECLAIMERS = he 2geibr #2geibr he #DS_AND_OP_INSTRUMENTATION_RECLAIMERS have read() and eras in node.
DAOI_POP_RECLAIMERS = nbr_pophe nbr_popplushe pop2geibr popplus2geibr #he #2geibr he #DS_AND_OP_INSTRUMENTATION_RECLAIMERS have read() and eras in node.
DAOI_RUSLON_RECLAIMERS = #crystallineL #those which need to be placement allocated and then given a type like ruslon's reclaimers  
DAOI_RUSLON_RDPTR_RECLAIMERS = #wfe #crystallineW #those which need to be placement allocated and then given a type like ruslon's reclaimers  and need readptrToobj and ptr to prev block

#### build ds of type OOI_RECLAIMERS = debra 
define make-ooi-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_df.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS	-DDEAMORTIZE_FREE_CALLS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(OOI_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooi-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

### build ds of type OOI_RECLAIMERS = token1 and token 4
define make-ooit-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(TOKEN_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooit-target,$(ds),$(alloc),$(reclaim),$(pool))))))) 

############### token 4
define make-ooit4-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS -DDEAMORTIZE_FREE_CALLS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(TOKEN4_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooit4-target,$(ds),$(alloc),$(reclaim),$(pool))))))) 
####################################


#### build ds if type OOI_IBR_RECLAIMERS = qsbr rcu
define make-ooi-ibr-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS -DOOI_IBR_RECLAIMERS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef
$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(OOI_IBR_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooi-ibr-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

# $(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_df.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS -DOOI_IBR_RECLAIMERS -DDEAMORTIZE_FREE_CALLS


#### build ds if type NZB_RECLAIMERS = nbr nbrplus
define make-nzb-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DNZB_RECLAIMERS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(NZB_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-nzb-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

# $(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_df.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DNZB_RECLAIMERS	-DDEAMORTIZE_FREE_CALLS

#### build ds if type OOI_POP_RECLAIMERS = rcu_pop #he 
define make-ooi-nzb-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS -DOOI_IBR_RECLAIMERS -DOOI_POP_RECLAIMERS	
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef
$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(OOI_POP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooi-nzb-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type HP_RECLAIMERS = hazardptrs
define make-hp-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DHP_RECLAIMERS
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_df.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DHP_RECLAIMERS	-DDEAMORTIZE_FREE_CALLS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(HP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-hp-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

#### build ds if type IBR_HP_RECLAIMERS = ibr_hp
define make-ibr-hp-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DIBR_HP_RECLAIMERS
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_df.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DIBR_HP_RECLAIMERS	-DDEAMORTIZE_FREE_CALLS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(IBR_HP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ibr-hp-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type IBR_HP_POP_RECLAIMERS = ibr_pophp
define make-ibr-hp-pop-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DIBR_HP_RECLAIMERS -DIBR_HP_POP_RECLAIMERS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(IBR_HP_POP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ibr-hp-pop-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type IBR_RCU_HP_POP_RECLAIMERS = rcu_pophp. reclaimers which use HP like reads and rcu like updaeEpochandCounters API
define make-rcu-hp-pop-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DIBR_RCU_HP_POP_RECLAIMERS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(IBR_RCU_HP_POP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-rcu-hp-pop-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

ifeq ("x","y")
############################### NBR version begin ################
#generate FAA, version of nbr BEGIN
define make-nzb-faa-target =
ubench_$(1).alloc_$(2).reclaim_$(3)_faa.pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_faa.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DFAA_RESTARTABLE
all: ubench_$(1).alloc_$(2).reclaim_$(3)_faa.pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(NZB_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-nzb-faa-target,$(ds),$(alloc),$(reclaim),$(pool)))))))
###generate FAA, version of nbr END


###generate Fenceelision version of nbr Begin
define make-nzb-fe-target =
ubench_$(1).alloc_$(2).reclaim_$(3)_fe.pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_fe.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DFE_RESTARTABLE
all: ubench_$(1).alloc_$(2).reclaim_$(3)_fe.pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(NZB_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-nzb-fe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

###generate Fenceelision version of nbr End

####generate relaxed restartable version of nbr Begin
define make-nzb-re-target =
ubench_$(1).alloc_$(2).reclaim_$(3)_re.pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_re.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DRELAXED_RESTARTABLE
all: ubench_$(1).alloc_$(2).reclaim_$(3)_re.pool_$(4).out
endef

$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(NZB_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-nzb-re-target,$(ds),$(alloc),$(reclaim),$(pool)))))))
#generate relaxed restartable version of nbr End
############################### NBR version end################
endif #ifeq ("x","y")



#### build ds if type DAOI_RECLAIMERS = 2geibr #he 
define make-daoi-ibr-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RECLAIMERS -DDAOI_IBR_RECLAIMERS	
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef
$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-ibr-target,$(ds),$(alloc),$(reclaim),$(pool)))))))
# $(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_df.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RECLAIMERS -DDAOI_IBR_RECLAIMERS	-DDEAMORTIZE_FREE_CALLS


#### build ds if type DAOI_POP_RECLAIMERS = nbr_pophe pop2geibr
define make-daoi-pop-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_POP_RECLAIMERS -DDAOI_IBR_RECLAIMERS	
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef
$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_POP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-pop-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type DAOI_RUSLON_RECLAIMERS = crystallineL 
define make-daoi-ruslon-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RUSLON_RECLAIMERS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef
$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_RUSLON_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-ruslon-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type DAOI_RUSLONRDPTR_RECLAIMERS = wfe crystallineW 
#-DDAOI_RUSLON_RECLAIMERS id for alloc and dealloc custon definition
# -DDAOI_RUSLONRDPTR_RECLAIMERS is to separate read() calls for read and readptrtoobjAndptr()
define make-daoi-ruslonrdptr-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RUSLON_RECLAIMERS -DDAOI_RUSLONRDPTR_RECLAIMERS
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3)_df.pool_$(4).out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RUSLON_RECLAIMERS -DDAOI_RUSLONRDPTR_RECLAIMERS -DDEAMORTIZE_FREE_CALLS
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).out
endef
$(foreach ds,$(DATA_STRUCTURES),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_RUSLON_RDPTR_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-ruslonrdptr-target,$(ds),$(alloc),$(reclaim),$(pool)))))))












#### 
# build dgt for LONG_RUNNING_EXP 
#### 


#PPOPP25 begin
DATA_STRUCTURES_GBE=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/hmlist/adapter.h))


#### build ds if type OOI_IBR_RECLAIMERS = qsbr rcu
define make-ooi-ibr_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS -DOOI_IBR_RECLAIMERS -DLONG_RUNNING_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef
$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(OOI_IBR_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooi-ibr_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


define make-rcu-hp-pop_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DIBR_RCU_HP_POP_RECLAIMERS -DLONG_RUNNING_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef

$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(IBR_RCU_HP_POP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-rcu-hp-pop_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type IBR_HP_RECLAIMERS = ibr_hp
define make-ibr-hp_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DIBR_HP_RECLAIMERS -DLONG_RUNNING_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef

$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(IBR_HP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ibr-hp_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type IBR_HP_POP_RECLAIMERS = ibr_pophp
define make-ibr-hp-pop_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DIBR_HP_RECLAIMERS -DIBR_HP_POP_RECLAIMERS -DLONG_RUNNING_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef

$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(IBR_HP_POP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ibr-hp-pop_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type DAOI_RECLAIMERS = 2geibr #he 
define make-daoi-ibr_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RECLAIMERS -DDAOI_IBR_RECLAIMERS -DLONG_RUNNING_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef
$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-ibr_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type DAOI_POP_RECLAIMERS = nbr_pophe pop2geibr
define make-daoi-pop_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_POP_RECLAIMERS -DDAOI_IBR_RECLAIMERS -DLONG_RUNNING_EXP	
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef
$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_POP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-pop_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

define make-ooi_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS -DLONG_RUNNING_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef
$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(OOI_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooi_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type NZB_RECLAIMERS = nbr nbrplus
define make-nzb_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DNZB_RECLAIMERS -DLONG_RUNNING_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef

$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(NZB_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-nzb_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

#PPOPP 24 end







ifeq ("x","y")

DATA_STRUCTURES_GBE=$(patsubst ../ds/%/adapter.h,%,$(wildcard ../ds/guerr*/adapter.h))

#### build ds of type OOI_RECLAIMERS = debra 
define make-ooi_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS -DGARBAGE_BOUND_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef

$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(OOI_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooi_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

#### build ds if type OOI_IBR_RECLAIMERS = qsbr rcu
define make-ooi-ibr_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DOOI_RECLAIMERS -DOOI_IBR_RECLAIMERS -DGARBAGE_BOUND_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef
$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(OOI_IBR_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ooi-ibr_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type NZB_RECLAIMERS = nbr nbrplus
define make-nzb_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DNZB_RECLAIMERS -DGARBAGE_BOUND_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef

$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(NZB_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-nzb_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type IBR_HP_RECLAIMERS = ibr_hp
define make-ibr-hp_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DIBR_HP_RECLAIMERS -DGARBAGE_BOUND_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef

$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(IBR_HP_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-ibr-hp_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

#### build ds if type DAOI_RECLAIMERS = 2geibr #he 
define make-daoi-ibr_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RECLAIMERS -DDAOI_IBR_RECLAIMERS -DGARBAGE_BOUND_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef
$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-ibr_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

#### build ds if type DAOI_RUSLON_RECLAIMERS = crystallineL 
define make-daoi-ruslon_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RUSLON_RECLAIMERS -DGARBAGE_BOUND_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef
$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_RUSLON_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-ruslon_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))


#### build ds if type DAOI_RUSLONRDPTR_RECLAIMERS = wfe crystallineW 
#-DDAOI_RUSLON_RECLAIMERS id for alloc and dealloc custon definition
# -DDAOI_RUSLONRDPTR_RECLAIMERS is to separate read() calls for read and readptrtoobjAndptr()
define make-daoi-ruslonrdptr_gbe-target =
ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out: dir_guard
	$(GPP) ./main.cpp -o $(bin_dir)/ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out -I../ds/$1 -DDS_TYPENAME=$(1) -DALLOC_TYPE=$(2) -DRECLAIM_TYPE=$(3) -DPOOL_TYPE=$(4) $(FLAGS) $(LDFLAGS) -DDAOI_RUSLON_RECLAIMERS -DDAOI_RUSLONRDPTR_RECLAIMERS -DGARBAGE_BOUND_EXP
all: ubench_$(1).alloc_$(2).reclaim_$(3).pool_$(4).td.out
endef
$(foreach ds,$(DATA_STRUCTURES_GBE),$(foreach alloc,$(ALLOCATORS),$(foreach reclaim,$(DAOI_RUSLON_RDPTR_RECLAIMERS),$(foreach pool,$(POOLS),$(eval $(call make-daoi-ruslonrdptr_gbe-target,$(ds),$(alloc),$(reclaim),$(pool)))))))

endif #ifeq ("x","y")

clean:
	rm $(bin_dir)/*.out
	rm $(bin_dir)/*.txt
	mv *.csv old.csv
