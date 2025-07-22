#!/bin/bash

dir=$(dirname $(realpath $0))

time=2000
#keys=10000000
keys=20000

for bench in $(ls ${dir}/../microbench/bin/); do
    printf "$bench\n"
    #for threads in $(echo "1 2 4 8 16 32 72 108 143"); do
    #for threads in $(echo "16 32 72 108 143"); do
    for threads in $(echo "32 72 108 143"); do
        for allocator in $(echo "deqalloc hoard jemalloc mimalloc"); do
            #grep -E "find_throughput|rq_throughput|update_throughput|query_throughput|total_throughput|allocated_count|allocated_size|deallocated"
            printf "$threads $allocator ";
            LD_PRELOAD=${dir}/lib${allocator}.so numactl -i all ../microbench/bin/$bench -nwork $threads -nprefill 143 -i 50 -d 50 -rq 0 -rqsize 1 -k $keys -t $time -pin $(for i in $(seq 0 3); do seq $i 4 $((140+i)); done | tr '\n' ,) | grep -E "total_throughput";
        done
    done
done
