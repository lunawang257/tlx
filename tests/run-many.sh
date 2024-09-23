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
MAX_THREAD=4
N=1024000
sliceSize=32
sliceSizeMax=64
insertProp=33
lookupProp=34
scanProp=0

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_btreemix"

rm -f "$out"
echo "Output file: $out"

# shellcheck disable=SC2043
for slotMax in 256 32; do
    scanLen=$((slotMax*2))
    case $slotMax in
        32)
            sliceSize=8
            ;;
        64)
            sliceSize=8
            ;;
        128)
            sliceSize=32
            ;;
        256)
            sliceSize=32
            ;;
        512)
            sliceSize=64
            ;;
        *)
            sliceSize=32
    esac
    sliceSizeMax=$((sliceSize*2))
    # shellcheck disable=SC2043
    for valSize in 256 64 ; do
        # shellcheck disable=SC2043
        for dist in zipf uniform ; do
            for ((thread=1;thread<=MAX_THREAD;thread*=2)); do
                # shellcheck disable=SC2043
                for maplize_threshold in 0 100 ; do
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
--slice-size $sliceSize \
--slice-size-max $sliceSizeMax \
--iteration $N \
--num-threads $thread \
--maplize-threshhold $maplize_threshold \
-I $insertProp \
-L $lookupProp \
--scan-prop $scanProp \
--scan-len $scanLen \
--dist $dist \
--repeats $REPEAT"
                    if [ "$(uname -s)" == "Linux" ]; then
                        cmd="numactl -N -0 -m 0 $cmd"
                    fi
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
                        if [ "$(uname -s)" == "Linux" ]; then
                            gprof "$prog" gmon.out > "$outPath/gmon-${ts}-$runName.txt"
                        fi
                    fi
                done # for maplize_threshold
            done # for threads
        done # for dist
    done # for valSize
done # for slotMax
if [ -f "gmon.out" ]; then
    rm gmon.out
fi
