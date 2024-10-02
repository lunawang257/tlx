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

base=$(basename -- "$outPath")
out="$outPath/${base}-results-$ts.txt"

# smaller will reduce run time
REPEAT=0.1
if [ "$(uname -s)" == "Linux" ]; then
    MAX_THREAD=$(numactl --hardware | awk '/node 0 cpus:/ {print NF-3}')
else
    MAX_THREAD=4
fi
echo Max CPU is $MAX_THREAD
N=2048000
sliceSize=64

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest_btreemix"

rm -f "$out"
echo "Output file: $out"

# shellcheck disable=SC2043
for valSize in 256 ; do
    # shellcheck disable=SC2043
    for dist in zipf uniform ; do
        for maplize_threshold in 0 100 ; do
            # shellcheck disable=SC2043
            for slotMax in 4096 2048 1024 512 256 128 64 32 16 8 4; do
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
                for ((sliceSize=4;sliceSize<=slotMax;sliceSize=sliceSize*2)) ; do
                    if [[ "$maplize_threshold" -eq "100" && "$sliceSize" -ne "4" ]]; then
                        continue
                    fi
                    sliceSizeMax=$(( sliceSize + 1 ))
                    for thread in $MAX_THREAD; do
                        # shellcheck disable=SC2043
                        props=(
                            "100 0 0 0"
                            "0 100 0 0"
                            "0 0 0 0"
                            "0 0 100 100"
                            "0 0 100 100000"
                            "50 50 0 0"
                            "5 95 0 0"
                            "5 0 95 100"
                        )
                        props=(
                            "100 0 0 100000"
                        )
                        for prop_str in "${props[@]}" ; do
                            prop=($prop_str)
                            insertProp=${prop[0]}
                            lookupProp=${prop[1]}
                            scanProp=${prop[2]}
                            scanLen=${prop[3]}
                            printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
                            runName="SlotMax-$slotMax-ValSize-$valSize-SliceSz-$sliceSize"
                            runName="${runName}-SlcSzMx-$sliceSizeMax-Thread-$thread"
                            runName="${runName}-MplThrh-$maplize_threshold-Dist-$dist"
                            runName="${runName}-InsertP-$insertProp-LookupP-$lookupProp"
                            runName="${runName}-ScanProp-$scanProp-ScanLen-$scanLen"
                            oneResult="$outPath/all-res/$ts-$runName.txt"
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
                        done # for insert/lookup/scan prop
                    done # for threads
                done # for sliceSize
            done # for slotMax
        done # for maplize_threshold
    done # for dist
done # for valSize
if [ -f "gmon.out" ]; then
    rm gmon.out
fi
echo "Output file: $out"
