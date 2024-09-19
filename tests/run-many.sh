#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

if [ "$1" == "-n" ]; then
    dryrun=1
else
    dryrun=0
fi

# smaller will reduce run time
REPEAT=64
MAX_THREAD=6
N=102400

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_controller"

rm -f /tmp/out

for slotMax in 128 256 512 ; do
  for valSize in 0 128 256 512 ; do
    for maplize_threshold in 0 100 ; do
      for dist in zipf uniform ; do
        for ((thread=1;thread<=$MAX_THREAD;thread++)); do
          printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
          cmd="$prog \
--test btreemix \
--slot-max $slotMax \
--val-size $valSize \
--iteration $N \
--num-threads $thread \
--slice-size 32 \
--slice-size-max 64 \
--maplize-threshhold $maplize_threshold \
-I 34 \
-L 33 \
--dist $dist > /tmp/one-out"
          echo "$cmd"
          if [ "$dryrun" != "1" ] ; then
            eval $cmd
            if [ ! -f "/tmp/out" ]; then
              tail -2 /tmp/one-out
              tail -2 /tmp/one-out > /tmp/out
            else
              tail -1 /tmp/one-out
              tail -1 /tmp/one-out >> /tmp/out
            fi
          fi
        done
      done
    done
  done
done
