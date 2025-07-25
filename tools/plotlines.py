#!/usr/bin/python3

import numpy as np
import platform
import sys, getopt
import fileinput
import argparse
from cycler import cycler

######################
## parse arguments
######################

parser = argparse.ArgumentParser(description='Produce a multi-curve (x,y)-line plot from THREE COLUMN data provided via a file or stdin.')
parser.add_argument('-i', dest='infile', type=argparse.FileType('r'), default='throughput.txt', help='input file containing lines of form <series> <x> <y>; if none specified then will use stdin. (if your data is not in this order, try using awk to easily shuffle columns...)')
parser.add_argument('-o', dest='outfile', type=argparse.FileType('w'), default='op.png', help='output file with any image format extension such as .png or .svg; if none specified then plt.show() will be used')
parser.add_argument('-t', dest='title', default="Throughput Vs Threads", help='title string for the plot')
parser.add_argument('--x_label', dest='x_title', default="title", help='title for the x-axis')
parser.add_argument('--y_label', dest='y_title', default="title", help='title for the y-axis')
args = parser.parse_args()

# parser.print_usage()
if len(sys.argv) < 2:
    parser.print_usage()
    print('waiting on stdin for histogram data...')

print('args={}'.format(args))

WIN = (platform.system() == 'Windows' or 'CYGWIN' in platform.system())

######################
## load data
######################

series=dict() ## dict of pairs of arrays

i=0
# print(args.infile)
for line in args.infile:
    i=i+1
    line = line.rstrip('\r\n')
    if (line == '' or line == None):
        continue

    tokens = line.split(" ")
    if len(tokens) != 3:
        print("ERROR at line {}: '{}'".format(i, line))
        exit(1)

    ## add series if it does not already exist
    s = tokens[0]
    if s not in series:
        series[s] = ([], [])

    ## append data point to series
    series[s][0].append(int(tokens[1]))
    series[s][1].append(int(tokens[2]))

######################
## setup matplotlib
######################

import matplotlib as mpl
if WIN:
    mpl.use('TkAgg')
else:
    mpl.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
plt.style.use('dark_background')

######################
## setup plot
######################

dots_per_inch = 200
height_inches = 5
width_inches = 12

fig = plt.figure(figsize=(width_inches, height_inches), dpi=dots_per_inch)

print('number of series={}'.format(len(series)))
plt.gca().set_prop_cycle(color=['red', 'green', 'blue', 'orange', 'cyan', 'brown', 'purple', 'pink', 'gray', 'olive'], marker=['o', '+', 'x', '*', '.', 'X', 'h', 'D', 's', '^'])

for s, xy in series.items():
    print('    plotting series {} with {} points'.format(s, len(xy[0])))
    plt.plot(xy[0], xy[1], label=s)

######################
## y axis major, minor
######################

ax = plt.gca()

ax.grid(which='major', axis='y', linestyle='-', color='lightgray')

ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
ax.grid(which='minor', axis='y', linestyle='dotted', color='gray')

######################
## x axis major, minor
######################

ax.grid(which='major', axis='x', linestyle='-', color='lightgray')

ax.xaxis.set_minor_locator(ticker.AutoMinorLocator())
ax.grid(which='minor', axis='x', linestyle='dotted', color='gray')


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

if args.title == "" or args.title == None:
    ax.title.set_visible(False)
else:
    ax.title.set_visible(True)
    ax.set_title(args.title)


######################
## do the plot
######################
plt.legend()
plt.tight_layout()

if args.outfile == None:
    if WIN:
        mng = plt.get_current_fig_manager()
        mng.window.state('zoomed')
    plt.show()
else:
    print("saving figure image %s\n" % args.outfile.name)
    plt.savefig(args.outfile.name)
