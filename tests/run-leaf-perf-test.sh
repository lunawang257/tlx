#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_leaf"

iter=1000000

for slotMax in 512; do
   for valSize in 512; do
      slotSize=1
      while [ "$slotSize" -le "$slotMax" ]; do
         slotSize=$(( slotSize * 2 ))
         if [ "$slotSize" -ne "32" ]; then
            continue # only test 32 to save time
         fi
         for testType in update lookup scan; do
            for isMapl in 0 1; do
               echo slotMax=$slotMax valSize=$valSize slotSize=$slotSize testType=$testType isMapl=$isMapl
               $prog -i $iter -m $isMapl -t $testType -s $slotMax -v $valSize -S $slotSize
            done
         done
         echo slotMax=$slotMax valSize=$valSize slotSize=$slotSize testType=$testType isMapl=$isMapl
         $prog -i $iter -m 0 -t maplize -s $slotMax -v $valSize -S $slotSize
      done
   done
done
