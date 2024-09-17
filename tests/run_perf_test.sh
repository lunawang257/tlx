#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# smaller will reduce run time
REPEAT=64
N=1024000

LOOKUP=0
INSERT=100


threads=(1 2 4 8 12 16 20 24 28 32)
#threads=(20 24 28 32)

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_concurrent"
prog_debug="$SCRIPT_DIR/../build/Debug/tests/tlx_container_btree_speedtest_concurrent"


for slotMax in 256 512 ; do
    log_file="./eval_btree_insert_${slotMax}_0915.log"
    echo "Starting experiments for slotMax=${slotMax}" > ${log_file}
    for thread in "${threads[@]}"; do
    echo "Running with thread=${thread}, slotMax=${slotMax}" | tee -a ${log_file}
	printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
	echo "$prog -r $REPEAT -m $N -M $N -i $INSERT -l $LOOKUP -s -S $slotMax -t $thread"
	numactl -N 0 -m 0 $prog -r $REPEAT -m $N -M $N -i $INSERT -l $LOOKUP -s -S $slotMax -t $thread | tee -a ${log_file}	
    #gdb --args $prog_debug -r $REPEAT -m $N -M $N -i $INSERT -l $LOOKUP -s -S $slotMax -t $thread
    done
done