#!/bin/bash
# Usage: run-many.sh [-n] [output-dir-name] [baseName]

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

if [ "$1" == "-n" ]; then
    dryrun=1
    shift
else
    dryrun=0
fi

if [ "$1" != "" ]; then
    outPath=$1
    shift
else
    outPath=$HOME/tlx-perf
    mkdir -p "$outPath"
fi

if [ "$1" != "" ]; then
    base=$1
else
    base=$(basename -- "$outPath")
fi

paperMode=0 # find best config (slotMax, sliceSize) for each tree
paperMode=1 # calculate results for all threads with best config for each tree

if [ "$paperMode" != "0" ]; then
    echo paper mode

    # optimized with 100% insert
    # on Linux is 128, on M2 is 16
    BEST_BTREE_SLOT_MAX=16

    BEST_MAPL_SLOT_MAX=2048
    BEST_MAPL_SLICE_SIZE=16
    BEST_MAPL_SLICE_SIZE_MAX=17

    # best overall performance, optimized with 25% insert, 25% delete
    # 25% lookup, and 25% scan of length 1000
    # M1 results:
    BEST_MAPL_SLOT_MAX=512
    BEST_MAPL_SLICE_SIZE=32
    BEST_MAPL_SLICE_SIZE_MAX=33

    # Linux results with balanced
    BEST_BTREE_SLOT_MAX=64
    BEST_MAPL_SLOT_MAX=8192
    BEST_MAPL_SLICE_SIZE=32
    BEST_MAPL_SLICE_SIZE_MAX=33
else
    echo non-paper mode
fi

mkdir -p "$outPath/all-res"

ts=$(date +"%Y-%m-%d-%H-%M")

out="$outPath/${base}-results-$ts.txt"

# smaller will reduce run time
if [ "$(uname -s)" == "Linux" ]; then
    NUMA_0_MAX_THREAD=$(numactl --hardware | awk '/node 0 cpus:/ {print NF-3}')
    MAX_THREAD=32
else
    NUMA_0_MAX_THREAD=4
    MAX_THREAD=4
fi
echo Max CPU is $NUMA_0_MAX_THREAD
N=$((25*1000*1000))

COMMON_REPEAT=0.1
SCAN_REPEAT=0.01 # scan is too slow, repeat less
# shellcheck disable=SC2043
props=(
    #ins fnd  scn len repeat
    "100   0    0   0"  # all insert
    "0   100    0   0"  # all lookup (YCSB-C)
    "0   100  100 100"  # all scan
    "50   50    0   0"  # YCSB-A
    "5    95    0   0"  # YCSB-B
    "5     0   95 100"  # YCSB-E
    #"0     0  0   0"    # all delete
)
if [ "$paperMode" == "0" ]; then
    # in non-paper mode, only get 100% insert results
    props=(
        "25 25 25 100"
    )
fi

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_btreemix"

rm -f "$out"
echo "Output file: $out"

# shellcheck disable=SC2043
for prop_str in "${props[@]}" ; do
    prop=($prop_str)
    insertProp=${prop[0]}
    lookupProp=${prop[1]}
    scanProp=${prop[2]}
    scanLen=${prop[3]}
    repeat=$COMMON_REPEAT
    if [ "$scanProp" != "0" ]; then
        repeat=$SCAN_REPEAT
    fi
    for valSize in 256 ; do
        # shellcheck disable=SC2043
        for dist in uniform zipf ; do
            for maplize_threshold in 0 100 ; do
                # shellcheck disable=SC2043
                for slotMax in 16384 8192 ; do #4096 2048 1024 512 256 128 64 32 ; do
                    startSliceSize=16
                    for ((sliceSize=startSliceSize;sliceSize<=128;sliceSize=sliceSize*2)) ; do
                        if [[ "$maplize_threshold" -eq "100" && "$sliceSize" -ne "$startSliceSize" ]]; then
                            continue
                        fi
                        if [[ "$sliceSize" -ge "$slotMax" ]]; then
                            continue
                        fi
                        sliceSizeMax=$(( sliceSize + 1 ))

                        if [ "$paperMode" != "0" ]; then
                            if [ "$maplize_threshold" == "0" ]; then # MAPL tree
                                slotMax=$BEST_MAPL_SLOT_MAX
                                sliceSize=$BEST_MAPL_SLICE_SIZE
                                sliceSizeMax=$BEST_MAPL_SLICE_SIZE_MAX
                            else # B-tree
                                slotMax=$BEST_BTREE_SLOT_MAX
                                sliceSize=$BEST_MAPL_SLICE_SIZE
                                sliceSizeMax=$BEST_MAPL_SLICE_SIZE_MAX
                            fi
                        fi

                        for thread in 32 28 24 20 16 12 8 4 2 1 ; do
                            if [[ $thread -gt "$MAX_THREAD" ]]; then
                                continue
                            fi
                            printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
                            runName="SlotMax-$slotMax-ValSize-$valSize-SliceSz-$sliceSize"
                            runName="${runName}-SlcSzMx-$sliceSizeMax-Thread-$thread"
                            runName="${runName}-MplThrh-$maplize_threshold-Dist-$dist"
                            runName="${runName}-InsertP-$insertProp-LookupP-$lookupProp"
                            runName="${runName}-ScanProp-$scanProp-ScanLen-$scanLen"
                            oneResult="$outPath/all-res/${base}-$ts-$runName.txt"
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
--repeats $repeat"
                            if [[ $thread -gt "$NUMA_0_MAX_THREAD" ]]; then
                                if [ "$(uname -s)" == "Linux" ]; then
                                    cmd="numactl -N -0 -m 0 $cmd"
                                fi
                            fi
                            echo "$cmd > $oneResult"
                            if [ "$dryrun" != "1" ] ; then
                                eval "$cmd" > "$oneResult"
                                if [ ! -f "$out" ]; then
                                    tail -2 "$oneResult"
                                    tail -2 "$oneResult" > "$out"
                                else
                                    tail -1 "$oneResult"
                                    tail -1 "$oneResult" >> "$out"
                                fi
                                ## generate perf profile on Linux
                                #if [ "$(uname -s)" == "Linux" ]; then
                                #    gmonOutName="${ts}-gmon-$runName.txt"
                                #    longGmon="${outPath}/all-res/${gmonOutName}"
                                #    shortGmon="${outPath}/${gmonOutName}"
                                #    #echo "longGmon=$longGmon"
                                #    #echo "shortGmon=$shortGmon"
                                #    gprof "$prog" gmon.out > "$longGmon"
                                #    "${SCRIPT_DIR}/filter_gmon.py" < "$longGmon" > "$shortGmon"
                                #fi
                            fi
                            if [ "$paperMode" == "0" ]; then # non-paper mode, only run one thread
                                break
                            fi
                        done # for threads
                        if [ "$paperMode" != "0" ]; then # paper mode, only run one sliceSize
                            break
                        fi
                    done # for sliceSize
                    if [ "$paperMode" != "0" ]; then # paper mode, only run one slotMax
                        break
                    fi
                done # for slotMax
            done # for maplize_threshold
            if [ "$paperMode" == "0" ]; then # non-paper mode, only run first dist
                break
            fi
        done # for dist
    done # for valSize
done # for insert/lookup/scan prop
if [ -f "gmon.out" ]; then
    rm gmon.out
fi
echo "Output file: $out"
