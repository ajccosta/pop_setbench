#!/bin/sh
##this script runs compiles, runs and then produces nice figures using Setbenches tool framework for Hariss Micahel List (HML) for long running read operations experiment in POP paper of PPOPP 2025.  


data_dir="data_hml_longops"
exp_file=pop_exp_run_hml_longops.py
echo "############################################"
echo "Executing and generating FIGURES for HML Long read operations experiment..."
echo "############################################"

python3 ../tools/data_framework/run_experiment.py $exp_file -rdp

# echo "copying FIGURES to plots/expected_plots/ "
# cp data/*.png plots/expected_plots/
mkdir -p plots/generated_plots/plot_$data_dir
echo "copying FIGURES to plots/generated_plots/plot_$data_dir/ "
cp $data_dir/*.png plots/generated_plots/plot_$data_dir/