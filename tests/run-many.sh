#!/bin/bash
# Usage: run-many.sh [-n] [out-name]

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

out=/tmp/out

if [ "$1" == "-n" ]; then
    dryrun=1
    shift
else
    dryrun=0
fi

if [ "$1" != "" ]; then
    out=$1
fi

# smaller will reduce run time
REPEAT=16
MAX_THREAD=6
N=1024000
sliceSize=32
sliceSizeMax=64
insertProp=34
lookupProp=33

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_controller"

rm -f "$out"

# shellcheck disable=SC2043
for slotMax in 512 ; do
    # shellcheck disable=SC2043
    for valSize in 512 ; do
        # shellcheck disable=SC2043
        for maplize_threshold in 0 100 ; do
            # shellcheck disable=SC2043
            for dist in zipf uniform ; do
                for ((thread=1;thread<=MAX_THREAD;thread++)); do
                    printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
                    runName="SlotMax-$slotMax-ValSize-$valSize-SliceSz-$sliceSize"
                    runName="${runName}-SlcSzMx-$sliceSizeMax-Thread-$thread"
                    runName="${runName}-MplThrh-$maplize_threshold-Dist-$dist"
                    runName="${runName}-InsertP-$insertProp-LookupP-$lookupProp"
                    echo $runName
                    cmd="$prog \
--test btreemix \
--slot-max $slotMax \
--val-size $valSize \
--iteration $N \
--num-threads $thread \
--slice-size $sliceSize \
--slice-size-max $sliceSizeMax \
--maplize-threshhold $maplize_threshold \
-I $insertProp \
-L $lookupProp \
--dist $dist \
--repeats $REPEAT
> /tmp/one-out"
                    echo "$cmd"
                    if [ "$dryrun" != "1" ] ; then
                        eval "$cmd"
                        if [ ! -f "$out" ]; then
                            tail -2 /tmp/one-out
                            tail -2 /tmp/one-out > "$out"
                        else
                            tail -1 /tmp/one-out
                            tail -1 /tmp/one-out >> "$out"
                        fi
                    fi
                    # generate perf profile on Linux
                    if [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
                        gprof "$prog" gmon.out | sed 's/(unsigned short)//g' | > "$out-gmon-$runName.txt"
                        rm gmon.out
                    fi
                done
            done
        done
    done
done
