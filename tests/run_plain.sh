#!/bin/bash

# 1st arg is distribution, second arg is num threads, third arg is theta for zipfian, fourth arg is nolock or normal type
# numactl -N 0 -m 0 ../build/Release/tests/tlx_container_btree_test_plain --zipf_insert 8 0.99

#bash ./run_plain.sh nolock

# Array of thread counts
#threads=(1 2 4 8 12 16 20 24 28 32)
threads=(16)

# Array of theta values
thetas=(0 0.95 0.99)

# Iterate over each combination of threads and theta
for theta_value in "${thetas[@]}"; do
    log_file="./eval_zipf_${theta_value}_hyth_0913.log"
    echo "Starting experiments for theta=${theta_value}" > ${log_file}

    for thread_num in "${threads[@]}"; do
    echo "Running with threads=${thread_num}, theta=${theta_value}" | tee -a ${log_file}

    # Command to run the basic program with the current threads and theta values
    numactl -N 0 -m 0 ../build/Release/tests/tlx_container_btree_test_plain --zipf_inserts --threads_num ${thread_num} --theta ${theta_value} | tee -a ${log_file}
    #./basic --zipf_inserts --threads_num ${thread_num} --theta ${theta_value} | tee -a ${log_file}

    echo "Finished with threads=${thread_num}, theta=${theta_value}" tee -a ${log_file}
  done
done