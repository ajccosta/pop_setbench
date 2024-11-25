#!/bin/sh
##this script runs compiles, runs and then produces nice figures using Setbenches tool framework for five  data structures: Brown’s (a,b)-tree (ABT), the external binary search tree by David, Guerraoui, and Trigonakis (DGT), the Harris-Michael list (HML), a lazy list (LL), and a hashtable (HMHT) based on HML.  

echo " "
echo "############################################"
echo "Compiling data structures with reclaimers..."
echo "############################################"

python3 ../tools/data_framework/run_experiment.py pop_exp_compile.py -c

# export PATH=$PATH:.
# `chmod +x get_lscpu_numa_nodes.sh`
# `get_lscpu_numa_nodes.sh | awk '{row[NR]=(NR > 1 ? row[NR-1] : 0)+NF} END { row[NR]-=2 ; row[NR+1]=row[1]/2 ; for (i in row) print row[i] }' | sort -n > numa_thread_count.txt`


data_dir="data_ll"
exp_file=pop_exp_run_ll.py
echo "############################################"
echo "Executing and generating FIGURES for LL..."
echo "############################################"
python3 ../tools/data_framework/run_experiment.py $exp_file -rdp

# echo "copying FIGURES to plots/expected_plots/ "
# cp data/*.png plots/expected_plots/
mkdir -p plots/generated_plots/plot_$data_dir
echo "copying FIGURES to plots/generated_plots/plot_$data_dir/ "
cp $data_dir/*.png plots/generated_plots/plot_$data_dir/


data_dir="data_hml"
exp_file=pop_exp_run_hml.py
echo "############################################"
echo "Executing and generating FIGURES for Harriss Michael List (HML)..."
echo "############################################"

python3 ../tools/data_framework/run_experiment.py $exp_file -rdp

# echo "copying FIGURES to plots/expected_plots/ "
# cp data/*.png plots/expected_plots/
mkdir -p plots/generated_plots/plot_$data_dir
echo "copying FIGURES to plots/generated_plots/plot_$data_dir/ "
cp $data_dir/*.png plots/generated_plots/plot_$data_dir/

data_dir="data_hmlht"
exp_file=pop_exp_run_hmlht.py
echo "############################################"
echo "Executing and generating FIGURES for harris Michael list based hash table HMLHT..."
echo "############################################"

python3 ../tools/data_framework/run_experiment.py $exp_file -rdp

# echo "copying FIGURES to plots/expected_plots/ "
# cp data/*.png plots/expected_plots/
mkdir -p plots/generated_plots/plot_$data_dir
echo "copying FIGURES to plots/generated_plots/plot_$data_dir/ "
cp $data_dir/*.png plots/generated_plots/plot_$data_dir/


data_dir="data_dgt"
exp_file=pop_exp_run_dgt.py
echo "############################################"
echo "Executing and generating FIGURES for DGT..."
echo "############################################"

python3 ../tools/data_framework/run_experiment.py $exp_file -rdp

# echo "copying FIGURES to plots/expected_plots/ "
# cp data/*.png plots/expected_plots/
mkdir -p plots/generated_plots/plot_$data_dir
echo "copying FIGURES to plots/generated_plots/plot_$data_dir/ "
cp $data_dir/*.png plots/generated_plots/plot_$data_dir/

data_dir="data_abt"
exp_file=pop_exp_run_abt.py
echo "############################################"
echo "Executing and generating FIGURES for ABT..."
echo "############################################"

python3 ../tools/data_framework/run_experiment.py $exp_file -rdp

# echo "copying FIGURES to plots/expected_plots/ "
# cp data/*.png plots/expected_plots/
mkdir -p plots/generated_plots/plot_$data_dir
echo "copying FIGURES to plots/generated_plots/plot_$data_dir/ "
cp $data_dir/*.png plots/generated_plots/plot_$data_dir/

# Other Data Structures not used in the paper.


# data_dir="data_hl"
# exp_file=pop_exp_run_hl.py
# echo "############################################"
# echo "Executing and generating FIGURES for HL..."
# echo "############################################"

# python3 ../tools/data_framework/run_experiment.py $exp_file -rdp

# # echo "copying FIGURES to plots/expected_plots/ "
# # cp data/*.png plots/expected_plots/
# mkdir plots/plot_$data_dir
# echo "copying FIGURES to plots/plot_$data_dir/ "
# cp $data_dir/*.png plots/plot_$data_dir/



# data_dir="data_dgttd"
# exp_file=pop_exp_run_dgttd.py
# echo "############################################"
# echo "Executing and generating FIGURES for DGT..."
# echo "############################################"

# python3 ../tools/data_framework/run_experiment.py $exp_file -rdp

# # echo "copying FIGURES to plots/expected_plots/ "
# # cp data/*.png plots/expected_plots/
# mkdir plots/plot_$data_dir
# echo "copying FIGURES to plots/plot_$data_dir/ "
# cp $data_dir/*.png plots/plot_$data_dir/

# data_dir="data_dgtntd"
# exp_file=pop_exp_run_dgtntd.py
# echo "############################################"
# echo "Executing and generating FIGURES for DGT..."
# echo "############################################"

# python3 ../tools/data_framework/run_experiment.py $exp_file -p

# # echo "copying FIGURES to plots/expected_plots/ "
# # cp data/*.png plots/expected_plots/
# mkdir plots/plot_$data_dir
# echo "copying FIGURES to plots/plot_$data_dir/ "
# cp $data_dir/*.png plots/plot_$data_dir/