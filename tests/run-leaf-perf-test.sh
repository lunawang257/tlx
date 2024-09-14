#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_leaf"

iter=1000000

for slotMax in 512
do
   for valSize in 512
   do
      for testType in update lookup
      do
         for isMapl in 0 1
         do
            $prog -i $iter -m $isMapl -t $testType -s $slotMax -v $valSize
         done
      done
      $prog -i $iter -m 0 -t maplize -s $slotMax -v $valSize
   done
done
