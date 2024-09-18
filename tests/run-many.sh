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
N=1024000

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_concurrent"

rm -f /tmp/out

for slotMax in 64 256 ; do
   for ((thread=1;thread<=$MAX_THREAD;thread++)); do
      printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
      echo "$prog -r 64 -m $N -M $N -i 0 -l 100 -t $thread > /tmp/one-out"
      if [ "$dryrun" != "1" ] ; then
         $prog -r 64 -m $N -M $N -i 0 -l 100 -t $thread > /tmp/one-out
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
