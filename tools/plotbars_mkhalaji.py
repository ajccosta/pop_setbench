#!/usr/bin/python3

import numpy as np
import sys
import getopt
import os
import fileinput
import argparse
from palettable.cartocolors.qualitative import * 

######################
## parse arguments
######################

parser = argparse.ArgumentParser(description='Produce a pandas bar plot from THREE COLUMN <series> <x> <y> data provided via a file or stdin.')
parser.add_argument('-i', dest='infile', type=argparse.FileType('r'), default=sys.stdin, help='input file containing lines of form <series> <x> <y>; if none specified then will use stdin. (if your data is not in this order, try using awk to easily shuffle columns...)')
parser.add_argument('-o', dest='outfile', type=argparse.FileType('w'), default='out.png', help='output file with any image format extension such as .png or .svg; if none specified then plt.show() will be used')
parser.add_argument('-t', dest='title', default="", help='title string for the plot')
parser.add_argument('--x-title', dest='x_title', default="", help='title for the x-axis')
parser.add_argument('--y-title', dest='y_title', default="", help='title for the y-axis')
parser.add_argument('--width', dest='width_inches', type=float, default=8, help='width in inches for the plot (at given dpi); default 8')
parser.add_argument('--height', dest='height_inches', type=float, default=6, help='height in inches for the plot (at given dpi); default 6')
parser.add_argument('--dpi', dest='dots_per_inch', type=int, default=100, help='DPI (dots per inch) to use for the plot; default 100')
parser.add_argument('--no-x-axis', dest='no_x_axis', action='store_true', help='disable the x-axis')
parser.set_defaults(no_x_axis=False)
parser.add_argument('--no-y-axis', dest='no_y_axis', action='store_true', help='disable the y-axis')
parser.set_defaults(no_y_axis=False)
parser.add_argument('--logy', dest='log_y', action='store_true', help='use a logarithmic y-axis')
parser.set_defaults(log_y=False)
parser.add_argument('--no-y-minor-ticks', dest='no_y_minor_ticks', action='store_true', help='force the logarithmic y-axis to include all minor ticks')
parser.set_defaults(no_y_minor_ticks=False)
parser.add_argument('--legend-only', dest='legend_only', action='store_true', help='use the data solely to produce a legend and render that legend')
parser.set_defaults(legend_only=False)
parser.add_argument('--legend-include', dest='legend_include', action='store_true', help='include a legend on the plot')
parser.set_defaults(legend_include=False)
parser.add_argument('--legend-columns', dest='legend_columns', type=int, default=1, help='number of columns to use to show legend entries')
parser.add_argument('--font-size', dest='font_size', type=int, default=20, help='font size to use in points (default: 20)')
parser.add_argument('--no-y-grid', dest='no_y_grid', action='store_true', help='remove all grids on y-axis')
parser.set_defaults(no_y_grid=False)
parser.add_argument('--no-y-minor-grid', dest='no_y_minor_grid', action='store_true', help='remove grid on y-axis minor ticks')
parser.set_defaults(no_y_minor_grid=False)
parser.add_argument('--error-bar-width', dest='error_bar_width', type=float, default=10, help='set width of error bars (default 10); 0 will disable error bars')
parser.add_argument('--stacked', dest='stacked', action='store_true', help='causes bars to be stacked')
parser.set_defaults(stacked=False)
parser.add_argument('--ignore', dest='ignore', help='ignore the next argument')
parser.add_argument('--style-hooks', dest='style_hooks', default='', help='allows a filename to be provided that implements functions style_init(mpl), style_before_plotting(mpl, plot_kwargs_dict) and style_after_plotting(mpl). your file will be imported, and hooks will be added so that your functions will be called to style the mpl instance. note that plt/fig/ax can all be extracted from mpl.')
args = parser.parse_args()

# parser.print_usage()
if len(sys.argv) < 2:
    if sys.stdin.isatty():
        parser.print_usage()
        print('waiting on stdin for data...')

######################
## setup matplotlib
######################

# print('args={}'.format(args))

import math
import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import pandas as pd
import matplotlib.ticker as ticker
from matplotlib.ticker import FuncFormatter
from matplotlib import rcParams
plt.rcParams['text.usetex'] = True
plt.rcParams['text.latex.preamble'] = '\\usepackage{libertine}'
mpl.rc('hatch', color='k', linewidth=1)

# rcParams['mathtext.fontset'] = 'stix'
# rcParams['font.family'] = 'STIXGeneral'

if args.style_hooks != '':
    sys.path.append(os.path.dirname(args.style_hooks))
    module_filename = os.path.relpath(args.style_hooks, os.path.dirname(args.style_hooks)).replace('.py', '')
    import importlib
    mod_style_hooks = importlib.import_module(module_filename)
    mod_style_hooks.style_init(mpl)

else:
    rcParams.update({'figure.autolayout': True})
    rcParams.update({'font.size': args.font_size})
    # plt.style.use('dark_background')
    plt.rcParams["figure.dpi"] = args.dots_per_inch



######################
## load data
######################
from io import StringIO

content = args.infile.readlines()

actual_data = []

ignores = []
ordering = []
renames = dict()

for line in content: 
    line = line.replace("\n", "").strip()
    if len(line.split(" ")) == 0:
        continue
    if line.startswith("TITLE"): 
        args.title = line.replace("TITLE ", "")
    if line.startswith("XLABEL"): 
        args.x_title = line.replace("XLABEL ", "")
    if line.startswith("YLABEL"):
        args.y_title = line.replace("YLABEL ", "")
        
    if line.startswith("IGNORE"): 
        ignores = line.split(" ")[1:]
    elif line.startswith ("SORT"): 
        ordering = line.split(" ")[1:]
    elif line.startswith ("RENAME"):
        renames[line.split(" ")[1]] = line.split(" ")[2]
    else: 
        # actual data 
        label = line.split(" ")[0]
        if label in ignores:
            continue
        if label in renames:
            line = line.replace(label, renames[label])
        actual_data.append(line)

if len(ordering) > 0:
    actual_data.sort(key=lambda row: ordering.index(row.split(" ")[0]))

for line in actual_data:
    print(line)
data = pd.read_csv(StringIO('\n'.join(actual_data)), names=['series', 'x', 'y'], sep=' ', index_col=None)

# check for NaN in any cells (to see if it's NOT well formed 3-col data)
# (misses values or entire columns become NaNs in pandas)
if data.isnull().values.any():
    ## could be well-formed two column data. try parsing as two cols and check for NaNs.
    # print("3-col NaNs found")

    data_old = data
    data['y'] = data['x'] #.drop(['y'], axis=1)
    if data.isnull().values.any():
        ## NaNs found under two column hypothesis. not valid data.
        # print("2-col NaNs found")
        print("ERROR: you must provide valid two or three column data. Invalid parsed data:")
        print(data_old)
        exit(1)
    else:
        ## data is well formed two column data, as far as we can tell. add the x column.
        x = []
        # print(data)
        series_row_counts = dict()
        for index, row in data.iterrows():
            s = row['series']
            if s not in series_row_counts: series_row_counts[s] = 0
            series_row_counts[s] += 1
            x.append(series_row_counts[s])
            # print('row: {}'.format(row))
        data['x'] = x

# exit(0)
## should have well formed three column data at this point

tmean = pd.pivot_table(data, columns='series', index='x', values='y', aggfunc='mean')[ordering]
print(tmean)
tmin = pd.pivot_table(data, columns='series', index='x', values='y', aggfunc='min')[ordering]
tmax = pd.pivot_table(data, columns='series', index='x', values='y', aggfunc='max')[ordering]

# print(tmean.head())
# print(tmin.head())
# print(tmax.head())

# ## sort dataframes by algorithms in this order:
# tmean = tmean.reindex(algs, axis=1)
# tmin = tmin.reindex(algs, axis=1)
# tmax = tmax.reindex(algs, axis=1)
# print(tmean.head())


# tmean = tmean.reindex(["10\%\;Updates", "50\%\;Updates", "100\%\;Updates"])
print(tmean)
## compute error bars
tpos_err = tmax - tmean
tneg_err = tmean - tmin
err = [[tpos_err[c], tneg_err[c]] for c in tmean]
# print("error bars {}".format(err))

if not len(data):
    print("ERROR: no data provided, so no graph to render.")
    quit()

if args.error_bar_width > 0 and len(err):
    for e in err[0]:
        if len([x for x in e.index]) <= 1:
            print("note : forcing NO error bars because index is too small: {}".format(e.index))
            args.error_bar_width = 0
elif not len(err):
    args.error_bar_width = 0


######################
## setup plot
######################

plot_kwargs = dict(
      legend=False
    , title=args.title
    , kind='bar'
    , figsize=(args.width_inches, args.height_inches)
    , width=0.75
    , edgecolor='black'
    , linewidth=1.2
    , zorder=10
    , logy=args.log_y, 
)
if args.stacked: plot_kwargs['stacked'] = True

legend_kwargs = dict(
      title=None
    , loc='upper center'
    , bbox_to_anchor=(0.5, -0.2)
    , fancybox=False
    , shadow=False
    , ncol=args.legend_columns, 
    columnspacing=0.7
)

fig, ax = plt.subplots()
if args.style_hooks != '': mod_style_hooks.style_before_plotting(mpl, plot_kwargs, legend_kwargs)

if args.error_bar_width == 0:
    chart = tmean.plot(fig=fig, ax=ax, **plot_kwargs)
else:
    plot_kwargs['yerr'] = err
    plot_kwargs['error_kw'] = dict(elinewidth=args.error_bar_width, ecolor='black')
    # orig_cols = [col for col in tmean.columns]
    # tmean.columns = ["_" + col for col in tmean.columns]
    # chart = tmean.plot(ax=ax, legend=False, title=args.title, kind='bar', yerr=err, error_kw=dict(elinewidth=args.error_bar_width+4, ecolor='black',capthick=2,capsize=(args.error_bar_width+2)/2), figsize=(args.width_inches, args.height_inches), width=0.75, edgecolor='black', linewidth=3, zorder=10, logy=args.log_y)
    # ## replot error bars for a stylistic effect, but MUST prefix columns with "_" to prevent duplicate legend entries
    # tmean.columns = orig_cols
    chart = tmean.plot(fig=fig, ax=ax, **plot_kwargs)

chart.grid(axis='y', zorder=0)

# chart.axhline(y=74493601848, color='red', linewidth=5, zorder=1000)
# chart.text(x=0.5,y=74493601848-30000000000,s="Naive VEB",fontsize=20, color='red', zorder=1000)

ax = plt.gca()
# ax.set_ylim(0, ylim)

# ax.yaxis.get_offset_text().set_y(-100)
# ax.yaxis.set_offset_position("left")

# i=0
# for c in tmean:
#     # print("c in tmean={}".format(c))
#     # print("tmean[c]=")
#     df=tmean[c]
#     # print(df)
#     # print("trying to extract column:")
#     # print("tmean[{}]={}".format(c, tmean[c]))
#     xvals=[x for x in df.index]
#     yvals=[y for y in df]
#     errvals=[[e for e in err[i][0]], [e for e in err[i][1]]]
#     print("xvals={} yvals={} errvals={}".format(xvals, yvals, errvals))
#     ax.errorbar(xvals, yvals, yerr=errvals, linewidth=args.error_bar_width, color='red', zorder=20)
#     i=i+1

chart.set_xticklabels(chart.get_xticklabels(), ha="center", rotation=0)

# faces = plt.cm.Paired.colors[1::2]

# FOR EXPERIMENTS
# faces = Pastel_10.mpl_colors[1:]

faces = Pastel_10.mpl_colors

COLOR_MAP = {
    "O-BWT": faces[0],
    "NAT-BST": faces[1],
    "BRON-BST": faces[2],
    "C-IST": faces[3],
    "Elim-ABT": faces[4],
    "VEB": faces[5], 
    "NAIVE-VEB": faces[6], 
    "IDEAL": faces[7],
}


# FOR ABLATION
# from palettable.cartocolors.sequential import *
# faces = SunsetDark_5.mpl_colors



bars = ax.patches
# patterns =['x', '/', '//', 'O', 'o', '\\', '\\\\', '..', '+', ' ' ][1:]
patterns =['x', '/', '//', 'O', 'o', '\\', '\\\\', '.', '+', ' ' ]


PATTERN_MAP = {
    "O-BWT": patterns[0],
    "NAT-BST": patterns[1],
    "BRON-BST": patterns[2],
    "C-IST": patterns[3],
    "Elim-ABT": patterns[4],
    "VEB": patterns[5], 
    "NAIVE-VEB": patterns[6], 
    "IDEAL": patterns[7],
}

hatches = [p for p in patterns for i in range(len(tmean))]
facecolors = [f for f in faces for i in range(len(tmean))]
llabels = [l for l in tmean.columns for i in range(len(tmean))]
i = 0

for llabel, bar, hatch, face  in zip(llabels, bars, hatches, facecolors):
    
    if llabel in PATTERN_MAP:
        bar.set_hatch(PATTERN_MAP[llabel])
    else: 
        bar.set_hatch(hatch)
        
    if llabel in COLOR_MAP:
        bar.set_facecolor(COLOR_MAP[llabel])
    else: 
        bar.set_facecolor(face)
        
    bar.set_edgecolor("black")
    
    
    # if llabel == "Elim-ABT":
    #     bar.set_linestyle("dashed")
    #     bar.set_alpha(0.75)
        
        
    # if llabel == "IDEAL": 
    #     bar.set_linestyle("dashed")
    #     bar.set_hatch(" ")
    #     bar.set_facecolor("white")
    #     bar.set_alpha(0.75)
    # bar.set_facecolor(my_colors[i+1])
    # bar.set_edgecolor(my_colors[i])
    # i = (i + 2) % len(my_colors)

## maybe remove y grid

# print("args.no_y_grid={} args.no_y_minor_grid={}".format(args.no_y_grid, args.no_y_minor_grid))
if not args.no_y_grid:
    plt.grid(axis='y', which='major', linestyle='-')
    if not args.no_y_minor_grid:
        plt.grid(axis='y', which='minor', linestyle='--')

## maybe add all-ticks for logy

if not args.no_y_minor_ticks:
    if args.log_y:
        ax.yaxis.set_minor_locator(ticker.LogLocator(subs="all"))
    else:
        ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())

## maybe remove axis tick labels

if args.no_x_axis:
    plt.setp(ax.get_xticklabels(), visible=False)
if args.no_y_axis:
    plt.setp(ax.get_yticklabels(), visible=False)

## set x axis title

if args.x_title == "" or args.x_title == None:
    ax.xaxis.label.set_visible(False)
else:
    ax.xaxis.label.set_visible(True)
    ax.set_xlabel(args.x_title)

## set y axis title

if args.y_title == "" or args.y_title == None:
    ax.yaxis.label.set_visible(False)
else:
    ax.yaxis.label.set_visible(True)
    ax.set_ylabel(args.y_title)

## save plot

if args.legend_include:
    plt.legend(**legend_kwargs)

if not args.legend_only:
    if args.style_hooks != '': mod_style_hooks.style_after_plotting(mpl)

    print("saving figure {}".format(args.outfile.name))
    plt.savefig(args.outfile.name)

######################
## handle legend-only
######################

if args.legend_only:
    handles, labels = ax.get_legend_handles_labels()
    fig_legend = plt.figure() #figsize=(12,1.2))
    axi = fig_legend.add_subplot(111)
    fig_legend.legend(handles, labels, loc='center', ncol=legend_kwargs['ncol'], frameon=False)
    # fig_legend.legend(handles, labels, loc='center', ncol=int(math.ceil(len(tpos_err)/2.)), frameon=False)
    axi.xaxis.set_visible(False)
    axi.yaxis.set_visible(False)
    axi.axes.set_visible(False)
    fig_legend.savefig(args.outfile.name, bbox_inches='tight')