#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest"

iter=100000

rm -rf ./perfresults/*

for slotMax in 64 128 256 512; do
   for valSize in 128 256 512; do
      slotSize=$(( slotMax / 4 ))
      while [ "$slotSize" -le "$slotMax" ]; do
         if [ "$slotSize" -lt "8" ]; then
            continue # slice less than 8 is too small
         fi
         if [ "$slotSize" -gt "$(( slotMax * 4 ))" ]; then
            continue # less than 4 slices is too few
         fi
         for slotSizeMax in $(( slotSize + 1 )) $(( slotSize * 3 / 2 )) $(( slotSize * 2 )) $(( slotSize * 3 )); do
            for testType in update lookup scan rebalance; do
               logfile="./perfresults/run_result_${testType}_0923.log"
               for isMapl in 0 1; do
                  #echo slotMax=$slotMax valSize=$valSize slotSize=$slotSize testType=$testType isMapl=$isMapl
                  cmd="${prog}_${testType} -i $iter -m $isMapl -p $testType -s $slotMax -v $valSize -S $slotSize -M $slotSizeMax"
                  echo "$cmd" | tee -a ${logfile}
                  eval $cmd | tee -a ${logfile}
               done
            done
         #echo slotMax=$slotMax valSize=$valSize slotSize=$slotSize testType=$testType isMapl=$isMapl
         logfile_maplize="./perfresults/run_result_maplize_0923.log"
         cmd="${prog}_maplize -i $iter -m 0 -p maplize -s $slotMax -v $valSize -S $slotSize -M $slotSizeMax"
         echo "$cmd" | tee -a ${logfile_maplize}
         eval $cmd | tee -a ${logfile_maplize}
         done

         slotSize=$(( slotSize * 2 ))
      done
   done
done
