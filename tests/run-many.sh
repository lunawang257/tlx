#!/bin/bash

if [ "$1" == "-n" ]; then
    dryrun=1
else
    dryrun=0
fi

# smaller will reduce run time
REPEAT=64
MAX_THREAD=6
N=1024000

prog="../build/tests/tlx_container_btree_speedtest_concurrent"

rm -f /tmp/out

for rootSlot in 1 -2; do
    for lockReq in all root no-root none ; do
	for slotMax in 4 64 256 ; do
	    for ((thread=1;thread<=$MAX_THREAD;thread++)); do
		printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
		echo "$prog -r 64 -m $N -M $N -i 0 -l 100 -s -t $thread -R $rootSlot -L $lockReq > /tmp/one-out"
		if [ "$dryrun" != "1" ] ; then
		    $prog -r 64 -m $N -M $N -i 0 -l 100 -s -t $thread -R $rootSlot -L $lockReq > /tmp/one-out
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
