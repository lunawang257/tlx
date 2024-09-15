#!/usr/bin/env python3

import math

slots_list = [32, 64, 128, 256, 512]
size_list = [32, 64, 128, 256, 512]

# List of command formats to be included in the single file
command_formats = [
    'RUN_MAPLIZE({slots}, {size}, {slot_size}, {is_mapl});\n',
    'RUN_UPDATE({slots}, {size}, {slot_size}, {is_mapl});\n',
    'RUN_LOOKUP({slots}, {size}, {slot_size}, {is_mapl});\n',
    'RUN_SCAN({slots}, {size}, {slot_size}, {is_mapl});\n',
    # You can easily add new command formats here
]

with open('leaf-perf-run-all-options.hpp', 'w') as f:
    f.write('// DO NOT EDIT! please change gen-leaf-cmd.py to edit\n')
    for command_format in command_formats:
        for slots in slots_list:
            for size in size_list:
                max_slot_size_exp = int(math.log2(size // 2))
                slot_size_list = [2 ** i for i in range(1, max_slot_size_exp + 1)]
                for slot_size in slot_size_list:
                    if 'RUN_MAPLIZE' in command_format:
                        line = command_format.format(slots=slots, size=size, slot_size=slot_size, is_mapl=0)
                        f.write(line)
                    else:
                        for is_mapl in [0, 1]:
                            line = command_format.format(slots=slots, size=size, slot_size=slot_size, is_mapl=is_mapl)
                            f.write(line)
