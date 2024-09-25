import os
import numpy as np
import pandas as pd
import glob
import matplotlib.pyplot as plt
import re
import itertools
from matplotlib.backends.backend_pdf import PdfPages


my_colors = {"lock_unif" : "#009ADE",
             "nolock_unif" : "#009ADE",
             "lock_zipf_90" : "#00B000",
             "nolock_zipf_90" : "#00B000",
             "lock_zipf_95" : "#262626",
             "nolock_zipf_95" : "#262626",
             "lock_zipf_99" : "#FF1F58",
             "nolock_zipf_99" : "#FF1F58",
             }
my_colors5 = {"red" : "#FF1F58",
             "black" : "#262626",
             "blue" : "#009ADE",
             "green" : "#4ECB8D",
             "purple" : "#C701FF"
             }
my_fontsize = 13
my_linewidth = 1

traces = np.zeros(shape=(0,), dtype=str)
tput = np.zeros(shape=(0,), dtype=float)

def generate_slicesize_list(slotmax):
    """Generate a list of slice sizes based on specified rules."""
    slicesize_list = []
    initial_size = slotmax // 4
    current_size = initial_size
    
    while current_size <= slotmax:
        slicesize_list.append(current_size)
        current_size *= 2  # Double the size for the next entry

    return slicesize_list

def generate_slicesizemax_list(slicesize):
    slicesizemax_list = []
    slicesizemax_list.append(slicesize + 1)
    slicesizemax_list.append(int(slicesize * 1.5))
    slicesizemax_list.append(slicesize * 2)
    slicesizemax_list.append(slicesize * 3)
    return slicesizemax_list
    

def get_avgtime_slicesize(path, slotmax, valsize, operation):
    # Initialize lists to store times for each slicesize
    slicesize_list = generate_slicesize_list(slotmax)
    slicesize_values = []
    btree_times = []
    mapl_times = []
    btree_insert_times = []
    btree_del_times = []
    mapl_insert_times = []
    mapl_del_times = []
    maplize_times = []
    unmaplize_times = []
    rebalance_times = []
    
    # Loop over each slicesize
    for slicesize in slicesize_list:
        btree_time, mapl_time = None, None
        btree_insert_time, btree_del_time, mapl_insert_time, mapl_del_time = None, None, None, None
        maplize_time, unmaplize_time, rebalance_time = None, None, None
        
        # Regular expressions for the current slicesize
        if operation == 'update':
            btree_insert_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average insert time: ([\d\.]+) us")
            btree_del_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average delete time: ([\d\.]+) us")
            mapl_insert_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average maplized insert time: ([\d\.]+) us")
            mapl_delete_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average maplized delete time: ([\d\.]+) us")
        elif operation == 'maplize':
            maplize_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average maplize time: ([\d\.]+) us")
            unmaplize_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average unmaplize time: ([\d\.]+) us")
        elif operation == 'rebalance':
            rebalance_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average rebalance time: ([\d\.]+) us")
        else: # for lookup, scan
            btree_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average {operation} time: ([\d\.]+) us")
            mapl_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: \d+ Average maplized {operation} time: ([\d\.]+) us")
        
        # Open the file and extract data for the current slicesize
        with open(path, "r") as f:
            for line in f:
                if operation == 'update':
                    btree_insert_match = btree_insert_pattern.match(line)
                    if btree_insert_match:
                        btree_insert_time = float(btree_insert_match.group(1))
                    
                    btree_del_match = btree_del_pattern.match(line)
                    if btree_del_match:
                        btree_del_time = float(btree_del_match.group(1))
                    
                    mapl_insert_match = mapl_insert_pattern.match(line)
                    if mapl_insert_match:
                        mapl_insert_time = float(mapl_insert_match.group(1))
                    
                    mapl_del_match = mapl_delete_pattern.match(line)
                    if mapl_del_match:
                        mapl_del_time = float(mapl_del_match.group(1))
                elif operation == 'maplize':
                    maplize_match = maplize_pattern.match(line)
                    if maplize_match:
                        maplize_time = float(maplize_match.group(1))
                    
                    unmaplize_match = unmaplize_pattern.match(line)
                    if unmaplize_match:
                        unmaplize_time = float(unmaplize_match.group(1))
                elif operation == 'rebalance':
                    rebalance_match = rebalance_pattern.match(line)
                    if rebalance_match:
                        rebalance_time = float(rebalance_match.group(1))
                else:
                    btree_match = btree_pattern.match(line)
                    if btree_match:
                        #print(f"btree: {line}")
                        btree_time = float(btree_match.group(1))
                
                    mapl_match = mapl_pattern.match(line)
                    if mapl_match:
                        #print(f"mapl: {line}")
                        mapl_time = float(mapl_match.group(1))
                
        # Only add to the lists if we found valid times for this slicesize
        if operation == 'update':
            if btree_insert_time is not None and btree_del_time is not None and mapl_insert_time is not None and mapl_del_time is not None:
                slicesize_values.append(slicesize)
                btree_insert_times.append(btree_insert_time)
                btree_del_times.append(btree_del_time)
                mapl_insert_times.append(mapl_insert_time)
                mapl_del_times.append(mapl_del_time)
        elif operation == 'maplize':
            if maplize_time is not None and unmaplize_time is not None:
                slicesize_values.append(slicesize)
                maplize_times.append(maplize_time)
                unmaplize_times.append(unmaplize_time)
        elif operation == 'rebalance':
            if rebalance_time is not None:
                slicesize_values.append(slicesize)
                rebalance_times.append(rebalance_time)
        else: # for lookup, scan
            if btree_time is not None and mapl_time is not None:
                slicesize_values.append(slicesize)
                btree_times.append(btree_time)
                mapl_times.append(mapl_time)
    
    # Return lists for plotting
    if operation == 'update':
        return slicesize_values, btree_insert_times, btree_del_times, mapl_insert_times, mapl_del_times
    elif operation == 'maplize':
        return slicesize_values, maplize_times, unmaplize_times
    elif operation == 'rebalance':
        return slicesize_values, rebalance_times
    else:
        return slicesize_values, btree_times, mapl_times

def get_avgtime_slicesizemax(path, slotmax, valsize, slicesize, operation):
    # Initialize lists to store times for each slicesizemax
    slicesizemax_list = generate_slicesizemax_list(slicesize)
    slicesizemax_values = []
    btree_times = []
    mapl_times = []
    btree_insert_times = []
    btree_del_times = []
    mapl_insert_times = []
    mapl_del_times = []
    maplize_times = []
    unmaplize_times = []
    rebalance_times = []
    
    # Loop over each slicesizemax
    for slicesizemax in slicesizemax_list:
        btree_time, mapl_time = None, None
        btree_insert_time, btree_del_time, mapl_insert_time, mapl_del_time = None, None, None, None
        maplize_time, unmaplize_time, rebalance_time = None, None, None
        
        # Regular expressions for the current slicesizemax
        if operation == 'update':
            btree_insert_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average insert time: ([\d\.]+) us")
            btree_del_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average delete time: ([\d\.]+) us")
            mapl_insert_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average maplized insert time: ([\d\.]+) us")
            mapl_del_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average maplized delete time: ([\d\.]+) us")
        elif operation == 'maplize':
            maplize_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average maplize time: ([\d\.]+) us")
            unmaplize_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average unmaplize time: ([\d\.]+) us")
        elif operation == 'rebalance':
            rebalance_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average rebalance time: ([\d\.]+) us")
        else: # for lookup, scan
            btree_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average {operation} time: ([\d\.]+) us")
            mapl_pattern = re.compile(rf"Max Slots: {slotmax} Value Size: {valsize} Slice Size: {slicesize} Slice Size Max: {slicesizemax} Average maplized {operation} time: ([\d\.]+) us")
        
        # Open the file and extract data for the current slicesizemax
        with open(path, "r") as f:
            for line in f:
                if operation == 'update':
                    btree_insert_match = btree_insert_pattern.match(line)
                    if btree_insert_match:
                        btree_insert_time = float(btree_insert_match.group(1))
                    
                    btree_del_match = btree_del_pattern.match(line)
                    if btree_del_match:
                        btree_del_time = float(btree_del_match.group(1))
                    
                    mapl_insert_match = mapl_insert_pattern.match(line)
                    if mapl_insert_match:
                        mapl_insert_time = float(mapl_insert_match.group(1))
                    
                    mapl_del_match = mapl_del_pattern.match(line)
                    if mapl_del_match:
                        mapl_del_time = float(mapl_del_match.group(1))
                elif operation == 'maplize':
                    maplize_match = maplize_pattern.match(line)
                    if maplize_match:
                        maplize_time = float(maplize_match.group(1))
                    
                    unmaplize_match = unmaplize_pattern.match(line)
                    if unmaplize_match:
                        unmaplize_time = float(unmaplize_match.group(1))
                elif operation == 'rebalance':
                    rebalance_match = rebalance_pattern.match(line)
                    if rebalance_match:
                        rebalance_time = float(rebalance_match.group(1))
                else:
                    btree_match = btree_pattern.match(line)
                    if btree_match:
                        #print(f"btree: {line}")
                        btree_time = float(btree_match.group(1))
                                    
                    mapl_match = mapl_pattern.match(line)
                    if mapl_match:
                        #print(f"mapl: {line}")
                        mapl_time = float(mapl_match.group(1))
        
        # Only add to the lists if we found valid times for this slicesizemax
        if operation == 'update':
            if btree_insert_time is not None and btree_del_time is not None and mapl_insert_time is not None and mapl_del_time is not None:
                slicesizemax_values.append(slicesizemax)
                btree_insert_times.append(btree_insert_time)
                btree_del_times.append(btree_del_time)
                mapl_insert_times.append(mapl_insert_time)
                mapl_del_times.append(mapl_del_time)
        elif operation == 'maplize':
            if maplize_time is not None and unmaplize_time is not None:
                slicesizemax_values.append(slicesizemax)
                maplize_times.append(maplize_time)
                unmaplize_times.append(unmaplize_time)
        elif operation == 'rebalance':
            if rebalance_time is not None:
                slicesizemax_values.append(slicesizemax)
                rebalance_times.append(rebalance_time)
        else: # for lookup, scan
            if btree_time is not None and mapl_time is not None:
                slicesizemax_values.append(slicesizemax)
                btree_times.append(btree_time)
                mapl_times.append(mapl_time)
    
    # Return lists for plotting
    if operation == 'update':
        return slicesizemax_values, btree_insert_times, btree_del_times, mapl_insert_times, mapl_del_times
    elif operation == 'maplize':
        return slicesizemax_values, maplize_times, unmaplize_times
    elif operation == 'rebalance':
        return slicesizemax_values, rebalance_times
    else:
        return slicesizemax_values, btree_times, mapl_times 


def plot_avgtime_slicesize(path, slotmax, valsize, operation):
    # Get the throughput data
    if operation == 'update':
        slicesize_values, insert_btree, delete_btree, insert_mapl, delete_mapl = get_avgtime_slicesize(path, slotmax, valsize, operation)
    elif operation == 'maplize':
        slicesize_values, maplize_times, unmaplize_times = get_avgtime_slicesize(path, slotmax, valsize, operation)
    elif operation == 'rebalance':
        slicesize_values, rebalance_times = get_avgtime_slicesize(path, slotmax, valsize, operation)
    else:
        slicesize_values, btree, mapl = get_avgtime_slicesize(path, slotmax, valsize, operation)
    
    fig = plt.figure(figsize = (7,3.5))
    
    if operation == 'update':
        plt.plot(slicesize_values, insert_mapl, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label='MAPL Leaf Insert')
        plt.plot(slicesize_values, insert_btree, color=my_colors5['red'], marker='x', linestyle='--',linewidth=my_linewidth,label='Btree Leaf Insert')
        plt.plot(slicesize_values, delete_mapl, color=my_colors5['blue'], marker='x', linestyle='-',linewidth=my_linewidth,label='MAPL Leaf Delete')
        plt.plot(slicesize_values, delete_btree, color=my_colors5['blue'], marker='x', linestyle='--',linewidth=my_linewidth,label='Btree Leaf Delete')
    elif operation == 'maplize':
        plt.plot(slicesize_values, maplize_times, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label=f'MAPL Leaf Maplize Time')
        plt.plot(slicesize_values, unmaplize_times, color=my_colors5['red'], marker='x', linestyle='--',linewidth=my_linewidth,label=f'MAPL Leaf Unmaplize Time')
    elif operation == 'rebalance':
        plt.plot(slicesize_values, rebalance_times, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label=f'MAPL Leaf Rebalance Time')           
    else: 
        plt.plot(slicesize_values, mapl, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label=f'MAPL Leaf {operation}')
        plt.plot(slicesize_values, btree, color=my_colors5['red'], marker='x', linestyle='--',linewidth=my_linewidth,label=f'Btree Leaf {operation}')
            
    plt.grid(axis='y', linestyle='--')
    plt.xlabel("Slice Size", fontsize=my_fontsize)
    plt.ylim(0,None)
    plt.xticks(slicesize_values)
    plt.ylabel('Time (us)', fontsize=my_fontsize)
    plt.legend(fontsize=my_fontsize - 3)

    
    title_name = f"Average time with different SliceSize (SlotMax={slotmax},ValuSize={valsize})"
    plt.title(title_name)
    fig.tight_layout()
    plt.subplots_adjust(bottom=0.01)
    #save_name = f"./perfresults/leafperf_{operation}_test_{slotmax}_{valsize}_0923.pdf"
    #print(save_name)
    #fig.savefig(save_name, bbox_inches='tight', format='pdf', dpi=1000, pad_inches=0.0)
    #plt.show()
    #plt.close(fig)
    return fig

def plot_avgtime_slicesizemax(path, slotmax, valsize, slicesize, operation):
    # Get the throughput data
    if operation == 'update':
        slicesizemax_values, insert_btree, delete_btree, insert_mapl, delete_mapl = get_avgtime_slicesizemax(path, slotmax, valsize, slicesize, operation)
    elif operation == 'maplize':
        slicesizemax_values, maplize_times, unmaplize_times = get_avgtime_slicesizemax(path, slotmax, valsize, slicesize, operation)
    elif operation == 'rebalance':
        slicesizemax_values, rebalance_times = get_avgtime_slicesizemax(path, slotmax, valsize, slicesize, operation)
    else:
        slicesizemax_values, btree, mapl = get_avgtime_slicesizemax(path, slotmax, valsize, slicesize, operation)
    
    fig = plt.figure(figsize = (7,3.5))
    
    if operation == 'update':
        plt.plot(slicesizemax_values, insert_mapl, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label='MAPL Leaf Insert')
        plt.plot(slicesizemax_values, insert_btree, color=my_colors5['red'], marker='x', linestyle='--',linewidth=my_linewidth,label='Btree Leaf Insert')
        plt.plot(slicesizemax_values, delete_mapl, color=my_colors5['blue'], marker='x', linestyle='-',linewidth=my_linewidth,label='MAPL Leaf Delete')
        plt.plot(slicesizemax_values, delete_btree, color=my_colors5['blue'], marker='x', linestyle='--',linewidth=my_linewidth,label='Btree Leaf Delete')
    elif operation == 'maplize':
        plt.plot(slicesizemax_values, maplize_times, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label=f'MAPL Leaf Maplize Time')
        plt.plot(slicesizemax_values, unmaplize_times, color=my_colors5['red'], marker='x', linestyle='--',linewidth=my_linewidth,label=f'MAPL Leaf Unmaplize Time')
    elif operation == 'rebalance':
        plt.plot(slicesizemax_values, rebalance_times, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label=f'MAPL Leaf Rebalance Time')           
    else:  
        plt.plot(slicesizemax_values, mapl, color=my_colors5['red'], marker='x', linestyle='-',linewidth=my_linewidth,label=f'MAPL Leaf {operation}')
        plt.plot(slicesizemax_values, btree, color=my_colors5['red'], marker='x', linestyle='--',linewidth=my_linewidth,label=f'Btree Leaf {operation}')
        
    plt.grid(axis='y', linestyle='--')
    plt.xlabel("Slice Size Max", fontsize=my_fontsize)
    plt.ylim(0,None)
    plt.xticks(slicesizemax_values)
    plt.ylabel('Time (us)', fontsize=my_fontsize)
    plt.legend(fontsize=my_fontsize - 3)

    
    title_name = f"Average time with different SliceSizeMax ({slotmax} {valsize} {slicesize})"
    plt.title(title_name)
    fig.tight_layout()
    plt.subplots_adjust(bottom=0.01)
    #save_name = f"./perfresults/leafperf_{operation}_test_{slotmax}_{valsize}_0923.pdf"
    #print(save_name)
    #fig.savefig(save_name, bbox_inches='tight', format='pdf', dpi=1000, pad_inches=0.0)
    #plt.show()
    #plt.close(fig)
    return fig

def plot_in_one(input, operation, output_slicesize, output_slicesizemax):
    slotmax_list = [64, 128, 256, 512]
    valsize_list = [128, 256, 512]

    # Create a PDF to save all the graphs
    #output_slicesize = f"./perfresults/leafpref_{operation}_slicesize_0923.pdf"
    
    with PdfPages(output_slicesize) as pdf:
        for slotmax, valsize in itertools.product(slotmax_list, valsize_list):
            fig = plot_avgtime_slicesize(input, slotmax, valsize, operation)
            pdf.savefig(fig, bbox_inches='tight')
            plt.close(fig)
                
    print(f"All graphs saved to {output_slicesize}")

    # For slicesizemax
    #output_slicesizemax = f"./perfresults/leafpref_{operation}_slicesizemax_0923.pdf"
    #output_slicesizemax = f"{parts[0]}_slicesizemax_{parts[1]}.pdf"
    with PdfPages(output_slicesizemax) as pdf:
        for slotmax, valsize in itertools.product(slotmax_list, valsize_list):
            #print(f"{slotmax}, {valsize}")
            slicesize_list = generate_slicesize_list(slotmax)
            for slicesize in slicesize_list:
                fig = plot_avgtime_slicesizemax(input, slotmax, valsize, slicesize, operation)
                pdf.savefig(fig, bbox_inches='tight')
                plt.close(fig)
    print(f"All graphs saved to {output_slicesizemax}")

##----------------------------------------------------

operation=["lookup", "update", "scan", "maplize", "rebalance"]
#operation=["maplize", "rebalance"]
for op in operation:
    input = f"./perfresults/run_result_{op}_0923.log"
    output_slicesize = f"./perfresults/leafpref_{op}_slicesize_0923.pdf"
    output_slicesizemax = f"./perfresults/leafpref_{op}_slicesizemax_0923.pdf"
    print(f"operation: {op}, path: {input}")
    plot_in_one(input, op, output_slicesize, output_slicesizemax)

print("Finished")