# Publish on Ping: A Better Way to Publish Reservations in Memory Reclamation for Concurrent Data Structures

This repo contains the memory reclamation benchmark used for the experiments and techniques reported in the paper titled: "Publish on Ping: A Better Way to Publish Reservations in Memory Reclamation for Concurrent Data Structures" accepted in ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming 2025 (PPOPP 2025).

zenodo DOI  : [10.5281/zenodo.10203082??????]
zenodo link : [????https://zenodo.org/records/10203082?????????]

### Data Structures
- Trevor Brown's external (a,b) tree (ABT)
- David, Guerraoui and Trigonakis 's external binary search tree using ticket locks (DGT)
- lazylist (LL)
- harris list (HL)
- harris michael list (HML)
- hash table using harris michael list buckets (HMLHT)

### Memory reclamation algorithms
- Hazard Pointers    (reclaimer_ibr_hp.h)
- QSBR               (reclaimer_qsbr.h)
- EBR                (reclaimer_ibr_rcu.h)
- IBR                (reclaimer_2geibr.h)
- NBR                (reclaimer_nbr.h)
- NBR+               (reclaimer_nbrplus.h)
- WFE                (reclaimer_wfe.h)
- CrystallineL       (reclaimer_crystallineL.h)
- CrystallineW       (reclaimer_crystallineW.h)
- Hazard Eras        (reclaimer_he.h)
- HazarPtrPOP        (reclaimer_ibr_popplushp.h)
- HazardEraPOP       (reclaimer_nbr_popplushe.h)
- EpochPOP           (reclaimer_rcu_popplushp.h)
- HPAsym             (reclaimer_ibr_hpasyf.h)


### Code Structure:
- reclamation algorithms are in common/recordmgr/
- data structures are in /ds/
- harness for the benchmark with entry file is in microbench/
- All the executables are generated in microbench/bin/
- Experiments can be done from /pop_experiments/. All users wishing to run experiments will just need to be in this directory.
- tools/ contains all the plotting and experiment scripts

### Extending the benchmark
Users can add new data structures by following the existing data structures in directory: ds/
Users can add new reclamation algorithms along the lines of exitising algorithms in directory: common/recordmgr

> Credit: This repo was carved out of [setbench](https://gitlab.com/trbot86/setbench) of [Multicore Lab](https://mc.uwaterloo.ca/) to test and evaluate reclamation algorithms with neutralization based reclamation [PPOPP21(???? )].

## 🏁 Getting Started with running the default benchmark in paper.

These instructions will get you a copy of the artifact up and running on your machine for development and testing purposes. This can be done in two ways: 
1. use our docker provided image or
2. alternatively prepare your machine by manually installing dependencies to run our artifact.

``` NOTE: To better reproduce results of POP we suggest to run pop_setbench on a multicore NUMA machine with at least two NUMA nodes.```

# (1) Running on Docker
* Install the latest version of Docker on your system. We tested the artifact with the Docker version 24.0.7, build 24.0.7-0ubuntu2~20.04.1. Instructions to install Docker may be found at https://docs.docker.com/engine/install/ubuntu/. Or you may refer to the "Installing Docker" section at the end of this README.

  To check the version of docker on your machine use: 

    ``` ~$ docker -v```
* First, download the artifact named pop_setbench.zip from the ppopp2025 artifact submission link (or at ?????).

* Find docker image named pop_docker.tar.gz in pop_setbench/ directory. 
  And load the downloaded docker image with the following command.

    ```~$ sudo docker load -i pop_docker.tar.gz ```
* Verify that image was loaded.

    ```~$ sudo docker images```
* start a docker container from the loaded image

    ```~$ sudo docker run --name pop -i -t --privileged pop_setbench /bin/bash ```
* run ls to see several files/folders of the artifact: Dockerfile README.md, common, ds, install.sh, lib, microbench, pop_experiments, tools. 

    ```~$ ls ```
If this succeeds you can move to the quick test section and skip the following section which discusses alternative ways to prepare your machine to run the artifact.

# *Alternative Way:* Preparing Host Machine:
In case you may want to prepare the host machine itself to run the artifact locally follow these instructions.

First, download the artifact named pop_setbench.zip from ppopp2025 artifact submission link (or at ?????).

The artifact requires the following packages/softwares on your Linux machine to compile and run the artifact.

```
 Use your system's package manager to install:
 > build-essential dos2unix g++ libnuma-dev make numactl parallel python3 python3-pip time zip micro
```

```
 Use your pip3 to install:
 > numpy matplotlib pandas seaborn ipython ipykernel jinja2 colorama
```

### Installing

Required packages can be installed in two ways:

##### Alternative 1 (use install.sh):
```
~$ cd pop_setbench
~$ ./install.sh
```

##### Alternative 2 (manually):
```
Use the following commands: 

~$ sudo apt-get update

~$ sudo apt-get install -y build-essential dos2unix g++ libnuma-dev make numactl parallel \
 python3 python3-pip time zip

~$ pip3 install numpy matplotlib pandas seaborn ipython ipykernel jinja2 colorama
```

Once the required software/packages are installed we are ready to run the experiments and generate the figures discussed in  the submitted version of the paper.

## 🔧 Quick Test
Until now, we have prepared the setup needed to compile and run the artifact. Now, let's do a quick test where we will compile, run and generate results to verify that the experiments (described later) would work correctly.

We would run two types of experiments. First, experiment to evaluate throughput and memory consumption (Figure 1, 2 and 3 in the paper) and second experiment to evaluate long running reads (Figure 4 in the paper)

Change directory to pop_setbench (if you used the alternative way to prepare your machine to execute the artifact) otherwise if you are in the docker container you would already be in pop_setbench/ directory.

### Evaluate throughput and memory consumption: 
To quickly compile, run and see default results for throughput experiment follow these steps:

* *step1*. Assuming you are currently in pop_setbench, execute the following command:

    ```~$ cd pop_experiments```.
* *step2*. Run the following command: 

    ```~$ ./run.sh```

If completed successfully this should generate plots in /pop_setbench/pop_experiments/plots/generated_plots/, where each subdirectory corresponds to a data structure. 


The Quick test uses the default inputs provided from files in pop_experiments/inputs/.
There are two types of experiments: normal experiments (in directory pop_experiments/inputs/normalExp) and long running read operations experiment (in directory pop_experiments/inputs/longrunreadExp). 

Default content of the files is comma separated values with no space and no newline:

+ For normal experiment (those similar to throughput and memory consumption plots in Figure 1, 2, and 3 in paper) inputs are in folder pop_experiments/inputs/normalExp:

  * *reclaimer.txt*      : none,rcu_pophp,ibr_popplushp
  * *steps.txt*          : 1
  * *threadsequence.txt* : 2,4,8
  * *workloadtype.txt*   : 50
  * *listsize.txt*       : 2000
  * *abtTreesize.txt*    : 20000000
  * *htsize.txt*         : 6000000

### Evaluate long running read operations: 
To quickly compile, run and see default results for long running read operations experiment follow these steps:

* *step1*. Assuming you are currently in pop_setbench, execute the following command:

  ```~$ cd pop_experiments```.

* *step2*. Run the following command: 

  ```~$ ./run_longreadops.sh```

If completed successfully this should generate plots in pop_experiments/plots/generated_plots/plot_data_hml_longops for HML data structure.

+ For long running read operations experiment (similar to Figure 4 in paper) inputs are in folder pop_experiments/inputs/longrunreadExp: 

  * *reclaimer.txt*      : none,rcu_pophp,ibr_popplushp
  * *threads.txt*        : 8  # Note, just have one thread number at a time and NOT a sequence for this experiment.
  * *workloadtype.txt*   : 50
  * *listsize.txt*       : 2000,4000


**WARNING:** if you are running the experiment in the docker container **DO NOT** exit the terminal after the quick test finishes as we would need to copy the generated figures on the host machine to be able to see them.  

### Analyze generated figures:
In case you chose to run the experiment on your system locally then you can simply find the figures in /pop_setbench/pop_experiments/plots/generated_plots/ directory and analyse them. Plots are in their own data structure specific sub folders.

Otherwise if you are running inside The Docker container follow below steps to fetch figures: 

To copy generated figures on your host machine copy the plots from the docker container to your host system by following these steps.

* Verify the name of the docker container. Use the following command which would give us the name of the loaded docker container under NAMES column which is 'pop'.

    ```~$ sudo docker container ls```

Open a new terminal on the same machine. Move to any directory where you would want the generated plots to be copied (use cd). And execute the following command. 

* Copy the generated plots from the pop_experiments/plots/ folder to your current directory.

    ```~$ sudo docker cp pop:/pop_setbench/pop_experiments/plots/ .```

Now you can analyse the generated plots.

* Each plot for throughput experiments follows a naming convention: throughput-[data structure name]-u[x: means x% of inserts and x% of deletes and remaining lookups]-sz[max size of the data structure].png. For example, a plot showing throughput of DGT with max size 2M with 50% inserts and 50% deletes is named as: throughput-guerraoui_ext_bst_ticket-u50-sz2000000.png. 
* The corresponding memory usage plot follows the same naming convention except that the name starts with ""maxretireListSz. For example, a plot showing memory consumption of DGT with max size 2M with 50% inserts and 50% deletes is named as: maxretireListSz-guerraoui_ext_bst_ticket-u50-sz200000.

* Similarly the plot for long running read operation experiments follows a naming convention: readthroughput-[data structure name]-u[x: means x% of inserts and x% of deletes and remaining lookups].png and corresponding memory usage plot follows the naming convention: maxretireListSz-[data structure name]-u[x: means x% of inserts and x% of deletes and remaining lookups].png. For example, a plot showing mem_usage of HML list with 50% inserts and 50% deletes is named as: maxretireListSz-hmlist-u50.

## 🔧 Running the tests with configuration reported in submitted paper [full experiments takes ~12 hrs]:

### Throughput and memory consumption experiments (Figure 1,2 and 3 in paper):
To reproduce figures reported in the submitted version of the paper please change inputs as indicated below:

Inside pop_experiments/inputs/normalExp/ change:

  * *reclaimer.txt*      : none,ibr_rcu,rcu_popplushp,ibr_popplushp,nbr_popplushe,ibr_hp,he,ibr_hpasyf,nbrplus,2geibr
  * *steps.txt*          : 1,2,3
  * *threadsequence.txt* : 1,18,36,54,72,90,108,126,144,180,216,252,288
  * *workloadtype.txt*   : 5,50
  * *abtTreesize.txt*    : 20000000
  * *dgtTreesize.txt*    : 2000000
  * *htsize.txt*         : 6000000
  * *listsize.txt*       : 2000


> Please ensure that comma separated values are provided. Simply copy pasting the aforementioned values in each corresponding input files should work, make sure not to introduce any space or newline at the end of a line in input files as that could cause errors in the script.

> **Warning**: Using a list size more than 20K will take long time in prefilling the list. Therefore, we suggest to use a list size of less than or equal to 20K. 

### Steps to change inputs inside the docker container:
``` ~$ cd pop_experiments/inputs/normalExp/ ```

Now change the appropriate '.txt' file using micro text editor (or editor of your choice, we have micro text editors pre-installed in the docker image) using following example command:

``` ~$ micro reclaimer.txt ```

save your changes and repeat this process for other input files listed above.

Next, repeat the following steps as done in the Quick test.
### Evaluate throughput and memory consumption experiments (Figure 1,2 and 3 in paper):

* *step1*. Assuming you are currently in pop_setbench, execute the following command:

    ```~$ cd pop_experiments```.
* *step2*. Run the following command: 

    ```~$ ./run.sh```

If completed successfully this should generate plots in /pop_setbench/pop_experiments/plots/generated_plots/, where each subdirectory corresponds to a data structure. 

For the figures in the submitted paper we tested POP algorithms on a NUMA machine with the following configuration:

    * Architecture        : Intel x86_64
    * CPU(s)              : 144
    * Sockets(s)          : 4
    * Thread(s) per core  : 2
    * Core(s) per socket  : 18
    * Memory              : 188G

Note: as long as the pop_setbench is run on a 144 thread machine with 4 NUMA nodes the generated plots should match the expected plots.

### Evaluate long running read operations (Figure 4 in paper): 

+ For long running read operations experiment (Figure 4 in paper) in folder pop_experiments/inputs/longrunreadExp: 

  * *reclaimer.txt*      : none,ibr_rcu,rcu_popplushp,ibr_popplushp,nbr_popplushe,ibr_hp,he,ibr_hpasyf,nbrplus,2geibr
  * *threads.txt*        : 144  # Note, just have one thread number at a time and NOT a sequence for this experiment.
  * *workloadtype.txt*   : 50
  * *listsize.txt*       : 10000,50000,100000,400000,800000

* *step1*. Assuming you are currently in pop_setbench, execute the following command:

    ```~$ cd pop_experiments```.
* *step2*. Run the following command:
    
    ```~$ ./run_longreadops.sh```

If completed successfully this should generate plots in pop_experiments/plots/generated_plots/plot_data_hml_longops for HML data structure.

### ⛏️ Analyze generated figures:

Once the above test completes the resultant figures could be found in pop_experiments/plots/generated_plots/. All plots follow the naming convention mentioned in the quick test section.

For easy comparison of the generated plots, we have put the expected figures for this experiment in the pop_experiments/plots/expected_plots/ directory. Please copy this directory in the same way as we copied pop_experiments/plots/generated_plots

* Copy the generated plots from the pop_experiments/plots/expected_plots/ folder to your current directory.

    ```~$ sudo docker cp pop:/pop_setbench/pop_experiments/plots/expected_plots/ .```

Now, you can analyse the generated plots and compare them with the expected plots assuming you have access to similar hardware.


## 🎉 What does run.sh do?

Inputs for experiments are provided from the following files:

  * *reclaimer.txt*      : comma separated list of reclamation algorithm names
  * *steps.txt*          : comma separated list of number of steps each run needs to repeat.
  * *threadsequence.txt* : comma separated list of thread sequence you want to run experiements with. This sequence becomes the X-axis for the generated throughput figures.
  * *workloadtype.txt*   : comma separated list of workload types. For eg., to evaluate with workload type of 50% inserts and 50% deletes enter 50 in workloadtype.txt.
  * *treesize.txt*       : Max number of nodes in tree.
  * *listsize.txt*       : Max number of nodes in list.
  * *abtTreesize.txt*    : Max number of nodes in AB tree.
  * *dgtTreesize.txt*    : Max number of nodes in DGT tree.
  * *htsize.txt*         : Max size of HML based hash table.

run.sh will do the following:

1. Compile the benchmark with reclamation algorithms and data structures.
2. Run all reclamation algorithms (NBR+, EBR (RCU style implementation), IBR, Hazard Pointer (HP), HazardPtrPOP, HAzardEraPOP, EpochPOP, Hazard Eras (HE), HPAsym, None), for a sequence of threads (say, 1,18,36,54,72,90,108,126,144,180,216,252,288), for varying workloads (say, 50% inserts/50% deletes and 5% inserts/5% deletes/rest lookups) for LL, HML, HMLHT, DGT and ABT data structrues. One reclamation algorithm is run several times. Each run is called one step. For example, HazardPtrPOP executing with 18 threads for a workload type that has 50% inserts and 50% deletes is called one step in our experiments.
3. Produce figures in directory pop_experiments/plots/generated_plots.

## 🚀 Types of machines we evaluated pop_setbench on:

* Smallest NUMA machine we have tested POP algorithms has following configuration:
  * Architecture        : Intel x86_64
  * CPU(s)              : 8
  * Socket(s)           : 1
  * Thread(s) per core  : 2
  * Core(s) per socket  : 4
  * Memory              : 16G
* Largest NUMA machine we have tested POP algorithms has following configuration:
  * Architecture        : Intel x86_64
  * CPU(s)              : 192
  * Socket(s)           : 4
  * Thread(s) per core  : 2
  * Core(s) per socket  : 24
  * Memory              : 377G

## 🎉 Claims from the paper supported by the artifact:

Proposed PoP algorithms: HazardPtrPOP, HAzardEraPOP and EpochPOP.

- *claim 1*. For write-heavy work-loads shown in Figure 1 and Figure 2 (in paper), publish-on-ping (POP) algorithms consistently perform better or are similar and exhibit a lower memory footprint compared to the original algorithms on which they are based.
  - please check throughput and max retire list size (mmeory consumption) plots for 100% updates (50%inserts/50%deletes, files with "u50" in name) in directory pubonping_smr/pop_experiments/plots/generated_plots. 

- *claim 2*. In read-heavy workloads all POP algorithms are similar or, in some cases especially, at oversubscription marginally better than EBR and IBR, as shown in Figure 3 (in paper).
  - please check throughput and max retire list size (mmeory consumption) plots for 10% updates (5%inserts/5%deletes/90%lookups, files with "u5" in name) in directory pubonping_smr/pop_experiments/plots/generated_plots. 

- *claim 3*. The POP algorithms maintain high read throughput for long running read operations since they do not require read threads to restart when a thread reclaims memory, while also maintaining low memory consumption, unlike NBR+.
  - please check long running read operations plots in pubonping_smr/pop_experiments/plots/generated_plots/plot_data_hml_longops

These claims are for experiments on our 144 CPUs and 4 sockets machine with 188G memory.

## ✍️ References
1. https://gitlab.com/trbot86/setbench
2. https://gitlab.com/aajayssingh/nbr_setbench
3. David, T., Guerraoui, R., & Trigonakis, V. (2015). Asynchronized concurrency: The secret to scaling concurrent search data structures. ACM SIGARCH Computer Architecture News, 43(1), 631-644.
4. Heller, S., Herlihy, M., Luchangco, V., Moir, M., Scherer, W. N., & Shavit, N. (2005, December). A lazy concurrent list-based set algorithm. In International Conference On Principles Of Distributed Systems (pp. 3-16). Springer, Berlin, Heidelberg.
5. https://github.com/urcs-sync/Interval-Based-Reclamation
6. Maged M Michael. 2004. Hazard pointers: Safe memory reclamation for lock-free objects. IEEE Transactions on Parallel and Distributed Systems 15, 6 (2004),491–504.
7. Trevor Brown. 2017. Techniques for Constructing Efficient Lock-free Data Structures. arXiv preprint arXiv:1712.05406 (2017).

## Installing Docker
Please follow these commands in order:

``` ~$ sudo apt update```

``` ~$ sudo apt-get install curl apt-transport-https ca-certificates software-properties-common ```

``` ~$ curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add - ```

``` ~$ sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"  ``` 

``` ~$ sudo apt update ```

``` ~$ sudo apt install docker-ce  ```

verify installation:

``` ~$ docker -v ```

## Misc:

### Build Docker image
``` sudo docker build -t pop_setbench . ```

### Save docker image
``` sudo docker save pop_setbench:latest | gzip > pop_docker.tar.gz ```

### erase all docker containers in the system
``` docker system prune -a ```