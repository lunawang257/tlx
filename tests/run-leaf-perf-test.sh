#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest"

iter=100000

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
         slotSizeMax=$(( slotSize * 2 ))
         for testType in update lookup scan; do
            for isMapl in 0 1; do
               #echo slotMax=$slotMax valSize=$valSize slotSize=$slotSize testType=$testType isMapl=$isMapl
               cmd="${prog}_${testType} -i $iter -m $isMapl -t $testType -s $slotMax -v $valSize -S $slotSize -M $slotSizeMax"
               echo "$cmd"
               eval $cmd
            done
         done
         #echo slotMax=$slotMax valSize=$valSize slotSize=$slotSize testType=$testType isMapl=$isMapl
         cmd="${prog}_maplize -i $iter -m 0 -t maplize -s $slotMax -v $valSize -S $slotSize -M $slotSizeMax"
         echo "$cmd"
         eval $cmd

         slotSize=$(( slotSize * 2 ))
      done
   done
done
