import os
import numpy as np
import pandas as pd
import glob
import matplotlib.pyplot as plt
import re
from matplotlib.backends.backend_pdf import PdfPages
import itertools

my_colors5 = {"red" : "#FF1F58",
             "black" : "#262626",
             "blue" : "#009ADE",
             "green" : "#4ECB8D",
             "purple" : "#C701FF"
             }
my_fontsize = 13
my_linewidth = 1

thrd_num = np.zeros(shape=(0,), dtype=int)
tput = np.zeros(shape=(0,), dtype=float)

def get_throughput(path, isunif, isbtree, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen):
  res_tput = []
  #print(f"parameters 1: isunif:{isunif}, isbtree:{isbtree}")
  #print(f"parameters 2: {slotmax}, {valsize}, {slicesize}, {slicesizemax}")
  #print(f"parameters 3: {insertprob}, {lookupprob}, {scanprob}, {scanlen}")

  with open(path, "r") as f:
    for line in f:
        if line.startswith("treemix"):
            fields = line.split()
            slot_max = int(fields[1])
            value_size = int(fields[2])
            slice_size = int(fields[3])
            slice_size_max = int(fields[4])
            insert_pro = int(fields[9])
            lookup_pro = int(fields[10])
            scan_prob = int(fields[11])
            scan_len = int(fields[12])
            if (slot_max, value_size, slice_size, slice_size_max, insert_pro, lookup_pro, scan_prob, scan_len) != (slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen):
                continue
            #print("line:" + line)
            num_thread = int(fields[5])
            mapl_thrh = int(fields[6])
            dist = fields[7]
            throughput = float(fields[13])
            if isunif == True and isbtree == True and dist == "Uniform" and mapl_thrh == 100:
                #print("unif btree" + dist + ";" + str(mapl_thrh))
                res_tput.append((num_thread, throughput))
            if isunif == False and isbtree == True and dist == "Zipf" and mapl_thrh == 100:
                #print("unif btree" + dist + ";" + str(mapl_thrh))
                res_tput.append((num_thread, throughput))
            if isunif == True and isbtree == False and dist == "Uniform" and mapl_thrh == 0:
                #print("zipf mapl" + dist + ";" + str(mapl_thrh))
                res_tput.append((num_thread, throughput))
            if isunif == False and isbtree == False and dist == "Zipf" and mapl_thrh == 0:
                #print("zipf mapl" + dist + ";" + str(mapl_thrh))
                res_tput.append((num_thread, throughput))
    #print(res_tput)
    thrd_num, tput = zip(*res_tput) if res_tput else ([], [])
    thrd_num = list(thrd_num)
    tput = list(tput)
    #print(f"tput: {tput}")
  return thrd_num, tput

def plot_throughput(path, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen):
    
    thrd_num, mapl_zipf = get_throughput(path, False, False, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen)
    thrd_num, btree_zipf = get_throughput(path, False, True, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen)
    thrd_num, mapl_unif = get_throughput(path, True, False, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen)
    thrd_num, btree_unif = get_throughput(path, True, True, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen)
    
    if len(thrd_num) == 0 or len(mapl_zipf) == 0 or len(btree_zipf) == 0 or len(mapl_unif) == 0 or len(btree_unif) == 0:
        return None
    fig = plt.figure(figsize = (7,3.5))
    
    plt.plot(thrd_num, mapl_zipf, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label='MAPL Zipf')
    plt.plot(thrd_num, btree_zipf, color=my_colors5['red'], marker='x', linestyle='--',linewidth=my_linewidth,label='Btree Zipf')
    plt.plot(thrd_num, mapl_unif, color=my_colors5['blue'], marker='x', linestyle='-',linewidth=my_linewidth,label='MAPL Unif')
    plt.plot(thrd_num, btree_unif, color=my_colors5['blue'], marker='x', linestyle='--',linewidth=my_linewidth,label='Btree Unif')
    
    
    plt.grid(axis='y', linestyle='--')
    plt.xlabel("Number of threads", fontsize=my_fontsize)
    plt.ylim(0,None)
    #plt.xticks([0, 1, 2, 4, 8, 12, 16])
    plt.xticks([1, 4, 8, 12, 16])
    plt.ylabel('Throughput (Mops/s)', fontsize=my_fontsize)
    plt.legend(fontsize=my_fontsize - 3)

    title_name = f"Throughput of Insert-only Workloads ({slotmax},{valsize},{slicesize},{slicesizemax})"
    plt.title(title_name)
    fig.tight_layout()
    plt.subplots_adjust(bottom=0.01)
    
    return fig
    
def plot_multi_graphs_in_one_file(input, output):
    
    slotmax_list = [512, 256, 128, 64, 32]
    valsize_list = [512, 256, 128, 64, 32]
    # Mapping slotmax values to slicesize and slicesizemax
    slotmax_to_slicesize = {
        512: (64, 65),
        256: (32, 33),
        128: (32, 33),
        64: (8, 9),
        32: (8, 9)
    }
    insertprob = 100
    lookupprob = 0
    scanprob = 0
    scanlen = 0

    # Create a PDF to save all the graphs    
    with PdfPages(output) as pdf:
        for slotmax, valsize in itertools.product(slotmax_list, valsize_list):
            slicesize, slicesizemax = slotmax_to_slicesize[slotmax]
            print(f"Dealing with: {slotmax}, {valsize}, {slicesize}, {slicesizemax}, {insertprob}, {lookupprob}, {scanprob}, {scanlen}")
            fig = plot_throughput(input, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen)
            if fig is None:
                continue
            pdf.savefig(fig, bbox_inches='tight')
            plt.show()
            plt.close(fig)
                
    print(f"All graphs saved to {output}")

def plot_single_graph(input, output, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen):
    print(f"Dealing with: {slotmax}, {valsize}, {slicesize}, {slicesizemax}, {insertprob}, {lookupprob}, {scanprob}, {scanlen}")
    fig = plot_throughput(input, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen)
    if fig is None:
        print(f"The result is empty.")
        return
    fig.savefig(output, bbox_inches='tight', format='pdf', dpi=1000, pad_inches=0.0)
    plt.close(fig)
    print(f"The graph saved to {output}")

def main():
    input = f"./run-eval-0926/results-2024-09-26-11-06.txt"
    #input = f"./run-eval-0926/results-2024-09-26-22-471.txt"
    output = f"./perf_insert.pdf"

    # Usage 1: Plot multiple graphs in one file
    # Adjust parameters in plot_multi_graphs_in_one_file() function
    plot_multi_graphs_in_one_file(input, output)
    
    # Usage 2: Plot a single graph
    # TODO: use find-best-slotmax.py to generate parameters
    slotmax = 512
    valsize = 512
    slicesize = 64
    slicesizemax = 65
    insertprob = 100
    lookupprob = 0
    scanprob = 0
    scanlen = 0
    output = f"./perf_single_{insertprob}_{lookupprob}_{scanprob}_{scanlen}.pdf"
    plot_single_graph(input, output, slotmax, valsize, slicesize, slicesizemax, insertprob, lookupprob, scanprob, scanlen)

    print("Finished")

if __name__ == "__main__":
    main()