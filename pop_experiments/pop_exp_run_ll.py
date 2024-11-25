# change following parameters only in define_experiment()
# RECLAIMER_ALGOS
# __trials
#  TOTAL_THREADS
# INS_DEL_HALF
#  DS_SIZE

###this script runs compiles, runs and then produces nice figures using Setbenches tool framework for lazylist.

import sys 
sys.path.append('../tools/data_framework')
from run_experiment import *

from _basic_functions import *
import pandas
import matplotlib as mpl

#extract max res size in MB
def get_maxres(exp_dict, file_name, field_name):
    ## manually parse the maximum resident size from the output of `time` and add it to the data file
    maxres_kb_str = shell_to_str('grep "maxres" {} | cut -d" " -f6 | cut -d"m" -f1'.format(file_name))
    return float(maxres_kb_str) / 1000

def my_plot_func(filename, column_filters, data, series_name, x_name, y_name, title, exp_dict=None):
    plt.rcParams['font.size'] = '12'
    # print(data.head(20))
    data=data.groupby(['RECLAIMER_ALGOS', 'TOTAL_THREADS'])['total_throughput'].mean().reset_index()
    table = pandas.pivot_table(data, index=x_name, columns=series_name, values=y_name, aggfunc='mean')
    
    ax = table.plot(kind='line', title=title+' '+filename.rsplit('/',1)[1])
    # ax = table.plot(kind='line', title=title, legend=None)
    ax.set_xlabel("num threads")
    ax.set_ylabel("throughput (operations per sec)")

    # markers=['o', '+', 'x', '*', '.', 'X', 'h', 'D', 's', '^', '1','p','v','>']

    # if len(ax.get_lines()) >= len (markers):
    #     # print ("number markers less than lines in my_plot_func")
    #     assert 0, "number markers less than lines in my_plot_func"
    # else:
    for i, line in enumerate(ax.get_lines()):
        print(line.get_label())

        if line.get_label() == "2geibr":
            line.set_label("IBR")
            line.set_ls("dashed")
            line.set_marker("*")
            line.set_color("violet")
        if line.get_label() == "rcu_popplushp":
            line.set_label("EpochPOP")
            line.set_marker(">")
            line.set_color("orange")
            line.set_linewidth(3)
        if line.get_label() == "crystallineL":
            line.set_label("crystL")
            line.set_marker("+")
            line.set_ls("dotted")
            line.set_color("magenta")

        if line.get_label() == "crystallineW":
            line.set_label("crystW")
            line.set_marker("x")
            line.set_ls("dotted")
            line.set_color("indigo")
        if line.get_label() == "debra":
            line.set_marker("*")
            line.set_color("blue")

        if line.get_label() == "he":
            line.set_label("HE")
            line.set_ls("dashed")
            line.set_marker("P")
            line.set_color("green")
        if line.get_label() == "nbr_popplushe":
            line.set_label("HazardEraPOP")
            line.set_marker("P")
            line.set_color("green")
            line.set_linewidth(3)

        if line.get_label() == "ibr_hp":
            line.set_label("HP")
            line.set_marker("D")
            line.set_color("blue")
            line.set_ls("dashed")
        if line.get_label() == "ibr_hpasyf":
            line.set_label("HPAsym")
            line.set_marker("+")
            line.set_color("blue")
            line.set_ls("dashed")
        if line.get_label() == "ibr_popplushp":
            line.set_label("HazardPOP")
            line.set_marker("D")
            line.set_color("blue")
            line.set_linewidth(3)

        if line.get_label() == "ibr_rcu":
            line.set_label("EBR")
            line.set_marker(">")
            line.set_color("orange")
            line.set_ls("dashed")

        if line.get_label() == "nbr":
            line.set_marker("D")
            line.set_color("dimgray")                
        if line.get_label() == "nbrplus":
            line.set_label("NBR+")
            line.set_marker(".")                
            line.set_color("red")
            line.set_ls("dashed")                
        if line.get_label() == "none":
            line.set_label("NR")
            line.set_ls("dotted")                                
            line.set_color("black")
        if line.get_label() == "qsbr":
            line.set_marker("p")
            line.set_color("brown")
        if line.get_label() == "wfe":
            line.set_marker("1")
            line.set_color("sienna")

    # figlegend = plt.figure(figsize=(3,2))
    patches, labels = ax.get_legend_handles_labels()

    ax.legend(loc='center left', bbox_to_anchor=(1.0, 0.5), fancybox=True, shadow=True)
    # ax.get_legend().remove()  
    # figlegend.legend(patches, labels=labels)
    # figlegend.savefig('legend.png')

    # plt.legend()
    plt.grid()
    mpl.pyplot.savefig(filename, bbox_inches="tight")
    print('## SAVED FIGURE {}'.format(filename))    

def my_memplot_func(filename, column_filters, data, series_name, x_name, y_name, title, exp_dict=None):
    plt.rcParams['font.size'] = '12'   
    # print(data.head(20))
    data=data.groupby(['RECLAIMER_ALGOS', 'TOTAL_THREADS'])['max_reclamation_event_size_total'].mean().reset_index()
    table = pandas.pivot_table(data, index=x_name, columns=series_name, values=y_name, aggfunc='mean')
    
    # ax = table.plot(kind='line', title=title)
    ax = table.plot(kind='line', title=title+' '+filename.rsplit('/',1)[1])
    ax.set_xlabel("num threads")
    ax.set_ylabel("max retireList size (nodes, logscale)")
    ax.set_yscale('log')

    for i, line in enumerate(ax.get_lines()):
        print(line.get_label())

        if line.get_label() == "2geibr":
            line.set_label("IBR")
            line.set_ls("dashed")
            line.set_marker("*")
            line.set_color("violet")
        if line.get_label() == "rcu_popplushp":
            line.set_label("EpochPOP")
            line.set_marker(">")
            line.set_color("orange")
            line.set_linewidth(3)
        if line.get_label() == "crystallineL":
            line.set_label("crystL")
            line.set_marker("+")
            line.set_ls("dotted")
            line.set_color("magenta")

        if line.get_label() == "crystallineW":
            line.set_label("crystW")
            line.set_marker("x")
            line.set_ls("dotted")
            line.set_color("indigo")
        if line.get_label() == "debra":
            line.set_marker("*")
            line.set_color("blue")

        if line.get_label() == "he":
            line.set_label("HE")
            line.set_ls("dashed")
            line.set_marker("P")
            line.set_color("green")
        if line.get_label() == "nbr_popplushe":
            line.set_label("HazardEraPOP")
            line.set_marker("P")
            line.set_color("green")
            line.set_linewidth(3)

        if line.get_label() == "ibr_hp":
            line.set_label("HP")
            line.set_marker("D")
            line.set_color("blue")
            line.set_ls("dashed")
        if line.get_label() == "ibr_hpasyf":
            line.set_label("HPAsym")
            line.set_marker("+")
            line.set_color("blue")
            line.set_ls("dashed")
        if line.get_label() == "ibr_popplushp":
            line.set_label("HazardPOP")
            line.set_marker("D")
            line.set_color("blue")
            line.set_linewidth(3)

        if line.get_label() == "ibr_rcu":
            line.set_label("EBR")
            line.set_marker(">")
            line.set_color("orange")
            line.set_ls("dashed")

        if line.get_label() == "nbr":
            line.set_marker("D")
            line.set_color("dimgray")                
        if line.get_label() == "nbrplus":
            line.set_label("NBR+")
            line.set_marker(".")                
            line.set_color("red")
            line.set_ls("dashed")                
        if line.get_label() == "none":
            line.set_label("NR")
            line.set_ls("dotted")                                
            line.set_color("black")
        if line.get_label() == "qsbr":
            line.set_marker("p")
            line.set_color("brown")
        if line.get_label() == "wfe":
            line.set_marker("1")
            line.set_color("sienna")

    # figlegend = plt.figure(figsize=(3,2))
    patches, labels = ax.get_legend_handles_labels()

    ax.legend(loc='center left', bbox_to_anchor=(1.0, 0.5), fancybox=True, shadow=True)

    # plt.legend()
    plt.grid()
    # ax.set_prop_cycle(color=['red', 'green', 'blue', 'orange', 'cyan', 'brown', 'purple', 'pink', 'gray', 'olive'], marker=['o', '+', 'x', '*', '.', 'X', 'h', 'D', 's', '^'])
    mpl.pyplot.savefig(filename, bbox_inches="tight")
    print('## SAVED FIGURE {}'.format(filename))    


def define_experiment(exp_dict, args):
    set_dir_tools    (exp_dict, os.getcwd() + '/../tools') ## tools library for plotting
    set_dir_compile  (exp_dict, os.getcwd() + '/../microbench')     ## working dir for compiling
    set_dir_run      (exp_dict, os.getcwd() + '/../microbench/bin') ## working dir for running
    set_cmd_compile  (exp_dict, './compile.sh')
    set_dir_data    ( exp_dict, os.getcwd() + '/data_ll' )               ## directory for data files

    fr = open("inputs/normalExp/reclaimer.txt", "r")
    reclaimers=fr.readline().rstrip('\n') #remove new line
    reclaimers=reclaimers.split(',') # split
    reclaimers = [i.strip() for i in reclaimers] #remove white space
    # reclaimers=fr.readline().split(',')
    fr.close()
    
    ft = open("inputs/normalExp/threadsequence.txt", "r")
    thread_list=ft.readline().rstrip('\n') #remove new line
    thread_list=thread_list.split(',') # split
    thread_list = [i.strip() for i in thread_list] #remove white space
    thread_list = [int(i) for i in thread_list]
    ft.close()

    fw = open("inputs/normalExp/workloadtype.txt", "r")
    worktype=fw.readline().rstrip('\n') #remove new line
    worktype=worktype.split(',') # split
    worktype = [i.strip() for i in worktype] #remove white space
    worktype = [int(i) for i in worktype]
    fw.close()

    fs = open("inputs/normalExp/steps.txt", "r")
    steps=fs.readline().rstrip('\n') #remove new line
    steps=steps.split(',') # split
    steps = [i.strip() for i in steps] #remove white space
    steps = [int(i) for i in steps]
    fs.close() 

    fsz = open("inputs/normalExp/listsize.txt", "r")
    dssize=fsz.readline().rstrip('\n') #remove new line
    dssize=dssize.split(',') # split
    dssize = [i.strip() for i in dssize] #remove white space
    dssize = [int(i) for i in dssize]
    fsz.close() 


    print("INPUTS:")
    print ("reclaimers=", reclaimers) 
    print("thread_list=", thread_list)
    print("workloadtype=", worktype)
    print("steps=", steps)
    print("list size=", dssize)




    add_run_param (exp_dict, 'DS_ALGOS', ['herlihy_lazylist'])
    #['nbr','nbrplus','nbr_orig','debra', 'none','2geibr','qsbr', 'ibr_rcu','he','ibr_hp','wfe','crystallineL', 'crystallineW']
    add_run_param (exp_dict, 'RECLAIMER_ALGOS', reclaimers) #['none', 'ibr_rcu', 'rcu_pophp', 'ibr_popplushp', 'nbr_popplushe', 'ibr_hp', 'he', 'ibr_hpasyf', 'nbrplus', '2geibr']
    add_run_param (exp_dict, '__trials', steps) #[1,2,3]
    add_run_param     ( exp_dict, 'thread_pinning'  , ['-pin ' + shell_to_str('cd ' + get_dir_tools(exp_dict) + ' ; ./get_pinning_cluster.sh', exit_on_error=True)] )
    add_run_param    (exp_dict, 'TOTAL_THREADS', thread_list) #[1, 18, 36, 72, 90, 108, 126, 144, 108, 216, 252, 288]
    # add_run_param     ( exp_dict, 'TOTAL_THREADS'   , [1] + shell_to_listi('cd ' + get_dir_tools(exp_dict) + ' ; ./get_thread_counts_numa_nodes.sh', exit_on_error=True) )
    add_run_param    (exp_dict, 'INS_DEL_HALF', worktype) #[5, 25, 50]. 5 means 5% inserts, 5% deletes and 90% lookups; 50 means 50% inserts, 50% deletes and 0% lookups.
    add_run_param    (exp_dict, 'DS_SIZE', dssize) #[200, 2000, 20000]

    set_cmd_run      (exp_dict, 'LD_PRELOAD=../../lib/libmimalloc.so numactl --interleave=all time ./ubench_{DS_ALGOS}.alloc_new.reclaim_{RECLAIMER_ALGOS}.pool_none.out -nwork {TOTAL_THREADS} -nprefill {TOTAL_THREADS} -i {INS_DEL_HALF} -d {INS_DEL_HALF} -rq 0 -rqsize 1 -k {DS_SIZE} -t 3000')

    add_data_field   (exp_dict, 'total_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'max_reclamation_event_size_total', coltype='INTEGER')
    # add_data_field   (exp_dict, 'maxresident_mb', coltype='REAL', extractor=get_maxres)
    add_plot_set(exp_dict, name='throughput-{DS_ALGOS}-u{INS_DEL_HALF}-sz{DS_SIZE}.png', series='RECLAIMER_ALGOS'
        #   , title='Throughput'
          , x_axis='TOTAL_THREADS'
          , y_axis='total_throughput'
          , plot_type=my_plot_func
          , varying_cols_list=['INS_DEL_HALF','DS_SIZE']
          ,plot_cmd_args='--x_label threads --y_label throughput' )

    add_plot_set(
            exp_dict
          , name='maxretireListSz-{DS_ALGOS}-u{INS_DEL_HALF}-sz{DS_SIZE}.png'
          , series='RECLAIMER_ALGOS'
        #   , title='Max retireList size (nodes, logscale)'
        #   , filter=filter_string
          , varying_cols_list=['INS_DEL_HALF','DS_SIZE']
          , x_axis='TOTAL_THREADS'
          , y_axis='max_reclamation_event_size_total'
          , plot_type=my_memplot_func
          , plot_cmd_args='--x_label threads --y_label throughput'
    )

# import sys ; sys.path.append('../tools/data_framework') ; from run_experiment import *
# run_in_jupyter(define_experiment_dgt, cmdline_args='-dp')