#!/usr/bin/env python3

import math
import os
import sys
import re
import shutil
import hashlib

script_dir = os.path.dirname(__file__)

# set to run to and re-run to reduce compile time
# please set to False before checkin
btreemix_fast_compile = True
leaf_fast_compile = True

btreemix_slot_max_list = [32, 128, 256, 512]
btreemix_value_size_list = [64, 128, 256, 512]

leaf_slot_max_list = [32, 64, 128, 256, 512]
leaf_value_size_list = [32, 64, 128, 256, 512]

if btreemix_fast_compile:
    btreemix_slot_max_list = [512]
    btreemix_value_size_list = [512]

if leaf_fast_compile:
    leaf_slot_max_list = [512]
    leaf_value_size_list = [512]

# List of command formats to be included in the single file
command_formats = [
    'RUN_BTREEMIX({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n', # btreemix test
    # all rest belong to leaf tests
    'RUN_MAPLIZE({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
    'RUN_UPDATE({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
    'RUN_LOOKUP({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
    'RUN_SCAN({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
    'RUN_REBALANCE({slot_max}, {value_size}, {slice_size}, {slice_size_max});\n',
]

def update_file_if_different(old_file, new_file):
    """
    Compare the contents of new_file and old_file.
    If they differ, copy new_file to old_file.

    :param old_file: Path to the old file.
    :param new_file: Path to the new file.
    """

    old_file_basename = os.path.basename(old_file)
    new_file_basename = os.path.basename(new_file)

    # Check if new_file exists
    if not os.path.isfile(new_file):
        raise FileNotFoundError(f"New file '{new_file}' does not exist.")

    # If old_file does not exist, copy new_file to old_file
    if not os.path.isfile(old_file):
        shutil.copy2(new_file, old_file)
        print(f"Old file '{old_file_basename}' did not exist. Copied '{new_file_basename}' to '{old_file_basename}'.")
        return

    # Compare file sizes first for a quick check
    old_size = os.path.getsize(old_file)
    new_size = os.path.getsize(new_file)
    if old_size != new_size:
        shutil.copy2(new_file, old_file)
        print(f"File sizes differ (old: {old_size} bytes, new: {new_size} bytes). Updated '{old_file_basename}'.")
        return

    # If sizes are the same, compare hashes
    old_hash = compute_md5(old_file)
    new_hash = compute_md5(new_file)

    if old_hash != new_hash:
        shutil.copy2(new_file, old_file)
        print(f"File contents differ. Updated '{old_file_basename}'.")
    else:
        print(f"No changes detected. '{old_file_basename}' is up to date.")

def compute_md5(file_path, chunk_size=8192):
    """
    Compute the MD5 hash of a file.

    :param file_path: Path to the file.
    :param chunk_size: Number of bytes to read at a time.
    :return: MD5 hash hexadecimal string.
    """
    hash_md5 = hashlib.md5()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(chunk_size), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def extract_name(input_str):
    # Define a regex pattern to match 'RUN_<OPERATION>('
    pattern = r'RUN_(\w+)\('

    match = re.search(pattern, input_str)

    if match:
        return match.group(1).lower()
    else:
        return None

def main():
    new_names = []

    total_lines = 0
    for command_format in command_formats:
        sub_name = extract_name(command_format)
        old_name = f'{script_dir}/btree_speedtest_{sub_name}_options.hpp'
        new_name = old_name + ".new"
        new_names.append((old_name, new_name))
        with open(new_name, 'w') as f:
            lines = 0
            f.write('''// DO NOT EDIT! please change gen-leaf-cmd.py to edit
    void run_all_args() {
    ''')
            sub_name_upper = sub_name.upper()
            f.write(f'    testOptions.insert({sub_name_upper});\n')
            if sub_name == "btreemix":
                slot_max_list = btreemix_slot_max_list
                value_size_list = btreemix_value_size_list
            else:
                slot_max_list = leaf_slot_max_list
                value_size_list = leaf_value_size_list
            for slot_max in slot_max_list:
                for value_size in value_size_list:
                    max_slice_size_exp = int(math.log2(slot_max// 2))
                    slice_size_list = [64] # [2 ** i for i in range(3, max_slice_size_exp + 2)]
                    for slice_size in slice_size_list:
                        if slice_size < slot_max / 8: # minimum slice size
                            continue
                        if slice_size > slot_max: # maxmum num slices is slot_max
                            continue
                        for slice_size_max in [slice_size + 1]: #, int(slice_size * 1.5), slice_size * 2, slice_size * 3]:
                            #if slice_size_max > slot_max: break
                            line = command_format.format(slot_max=slot_max,
                                                         value_size=value_size,
                                                         slice_size=slice_size,
                                                         slice_size_max = slice_size_max)
                            f.write('    ') # indentation
                            f.write(line)
                            lines += 1
                            total_lines += 1
            f.write('}\n')

    # update old file only if it has changed so Makefile doesn't build unchanged ones
    for name_pair in new_names:
        old_name = name_pair[0]
        new_name = name_pair[1]
        update_file_if_different(old_name, new_name)
        os.remove(new_name)

    print(f"Generated {total_lines} lines in {len(command_formats)} files")

if __name__ == "__main__":
    main()
