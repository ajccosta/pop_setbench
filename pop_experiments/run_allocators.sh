#!/bin/bash

#parameters
time=5000
keys=20000000
inserts=50
deletes=50
nthreads=$(( $(nproc) - 1 ))
repeat=5
allocators="deqalloc hoard jemalloc mimalloc"



dir=$(dirname $(realpath $0))

#these benchmarks dont run for whatever reason):
#   ubench_brown_ext_abtree_lf with some schemes segfaults (atleast w/ deqalloc, mimalloc, hoard, and jemalloc)
#   hmlist and herlihy_lazylist are too slow, tests take too long
#   dont run "leaky" scheme (reclaim_none), no memory reutilized is against the point
block_list="ubench_brown_ext_abtree_lf.alloc_new.reclaim_nbr.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_ibr_popplushp.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_nbrplus.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_nbr_pophe.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_nbr_popplushe.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_rcu_pophp.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_pop2geibr.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_rcu_pop.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_popplus2geibr.pool_none.out
ubench_brown_ext_abtree_lf.alloc_new.reclaim_ibr_pophp.pool_none.out
herlihy_lazylist
hmlist
reclaim_none"


#run these benchmarks and nothing else
force_list="ubench_brown_ext_abtree_lf.alloc_new.reclaim_ibr_hp.pool_none.out"

#force only running these benchmarks
if [[ " $@ " == *" --force "* ]]; then
    benchs=$force_list
else
    #run all benchmarks except blocked ones, in random order
    block_list=$(echo -e $block_list | tr ' ' '|') #prepare for grep -E
    benchs=$(ls ${dir}/../microbench/bin/ | grep -Ev "$block_list" | shuf)
fi

results_file=$dir/smr_experiment_$(echo $allocators | tr ' ' '-')-${nthreads}thr-${inserts}i${deletes}d${keys}k${time}t_$(date +"%Y%m%d_%H%M%S")

for bench in $(echo -e "$benchs"); do
    printf "$bench\n" | tee -a $results_file
    for allocator in $(echo "$allocators"); do
        for rep in $(seq $repeat); do
            printf "$allocator " | tee -a $results_file;
            res=$(LD_PRELOAD=${dir}/lib${allocator}.so numactl -i all ../microbench/bin/$bench -nwork $nthreads -nprefill 4 -i $inserts -d $deletes -rq 0 -rqsize 1 -k $keys -t $time | grep -E "total_throughput");
            res=$(echo -e "$res" | sed 's/total_throughput=//g')
            printf "$res\n" | tee -a $results_file;
        done
    done
done