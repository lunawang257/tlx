#!/usr/bin/env python3

import math

# set to run to and re-run to reduce compile time
# please set to False before checkin
fast_compile = True

slot_max_list = [32, 64, 128, 256, 512]
value_size_list = [32, 64, 128, 256, 512]
thread_list = [1, 2]

# List of command formats to be included in the single file
command_formats = [
    'RUN_MAPLIZE({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
    'RUN_UPDATE({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
    'RUN_LOOKUP({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
    'RUN_SCAN({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
    'RUN_BTREEMIX({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
]

if fast_compile:
    slot_max_list = [32, 256]
    value_size_list = [0, 32, 256]
    # only leave the command you want to measure
    command_formats = [
        #'RUN_MAPLIZE({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
        #'RUN_UPDATE({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
        #'RUN_LOOKUP({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
        #'RUN_SCAN({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
        'RUN_BTREEMIX({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n'
    ]

with open('leaf-perf-run-all-options.hpp', 'w') as f:
    f.write('// DO NOT EDIT! please change gen-leaf-cmd.py to edit\n')
    for command_format in command_formats:
        for slot_max in slot_max_list:
            for value_size in value_size_list:
                max_slice_size_exp = int(math.log2(slot_max// 2))
                slice_size_list = [2 ** i for i in range(3, max_slice_size_exp + 1)]
                for slice_size in slice_size_list:
                    for slice_size_max in [slice_size + 1, int(slice_size * 1.5), slice_size * 2]: #, slice_size * 3]:
                            if slice_size_max > slot_max: break
                            line = command_format.format(slot_max=slot_max,
                                                        value_size=value_size,
                                                        slice_size=slice_size,
                                                        slice_size_max = slice_size_max)
                            f.write(line)
