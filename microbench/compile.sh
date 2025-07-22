#!/bin/bash

echo
echo $@ #@J shall contain use_timelines, any macro to be enabled using xargs
echo $PWD
echo

hwthreads=`lscpu | grep -e "^CPU(s):" | cut -d":" -f2 | tr -d " "`
#echo "hwthreads=$hwthreads"
use=`expr $hwthreads - 1`
#echo "make -j $use all"
cpufreqghz=$(python3 -c "print($(lscpu | grep "CPU max MHz" | tr -s ' ' | cut -d' ' -f4)/(10**3))") #wonky way of getting max cpu freq
make -j $use all $@ has_libpapi=0 CPU_FREQ_GHZ=$cpufreqghz # force has_libpapi=0 temporary for continuous integration, until I get away from using VMs...
