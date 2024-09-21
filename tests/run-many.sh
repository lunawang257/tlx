#!/bin/bash
# Usage: run-many.sh [-n] [output-dir-name]

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

if [ "$1" == "-n" ]; then
    dryrun=1
    shift
else
    dryrun=0
fi


if [ "$1" != "" ]; then
    outPath=$1
else
    outPath=$HOME/tlx-perf
    mkdir -p "$outPath"
fi
mkdir -p "$outPath/all-res"

ts=$(date +"%Y-%m-%d-%H-%M")

out="$outPath/results-$ts.txt"

# smaller will reduce run time
REPEAT=16
MAX_THREAD=2
N=1024000
sliceSize=32
sliceSizeMax=64
insertProp=34
lookupProp=33

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_controller"

rm -f "$out"

# shellcheck disable=SC2043
for slotMax in 32 128 256 512 ; do
    # shellcheck disable=SC2043
    for valSize in 0 32 128 256 512 ; do
        sqrt_slot_max=$(echo "scale=0; sqrt($slotMax)" | bc -l)
        if ((sqrt_slot_max * sqrt_slot_max < slotMax)); then
            sliceSize=$((sqrt_slot_max+1))
        else
            sliceSize=$sqrt_slot_max
        fi
        sliceSizeMax=$((sliceSize*2))
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
                    oneResult="$outPath/all-res/$runName.txt"
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
--repeats $REPEAT"
                    echo "$cmd"
                    if [ "$dryrun" != "1" ] ; then
                        eval $cmd > "$oneResult"
                        if [ ! -f "$out" ]; then
                            tail -2 "$oneResult"
                            tail -2 "$oneResult" > "$out"
                        else
                            tail -1 "$oneResult"
                            tail -1 "$oneResult" >> "$out"
                        fi
                        # generate perf profile on Linux
                        if [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
                            gprof "$prog" gmon.out > "$outPath/gmon-${ts}-$runName.txt"
                        fi
                    fi
                done # for threads
                exit 0
            done # for dist
        done # for maplize_threshold
    done # for valSize
done # for slotMax
if [ -f "gmon.out" ]; then
    rm gmon.out
fi
