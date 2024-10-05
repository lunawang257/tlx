#!/bin/bash

# usage: run-leaf-perf-test.sh [-n] [outpath] [base]
# -n: dry run

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
    outPath=$HOME/tlx-leaf-perf
    mkdir -p "${outPath}"
fi

if [ "$1" != "" ]; then
    base=$1
else
    base=$(basename -- "$outPath")
fi

ts=$(date +"%Y-%m-%d-%H-%M")
logfile="${outPath}/${base}-result-${ts}.log"
touch "$logfile"

prog="$SCRIPT_DIR/../build/Release/tests/tlx_container_btree_speedtest"

iter=1000000

rm -rf ./perfresults/*

for testType in update lookup scan rebalance maplize; do
    # shellcheck disable=SC2043
    for valSize in 256; do
        for slotMax in 64 256 2048 8192; do
            slotSize=$(( slotMax / 4 ))
            for slotSize in 16 32 64; do
                if [ "$slotSize" -lt "8" ]; then
                    continue # slice less than 8 is too small
                fi
                if [ "$slotSize" -gt "$(( slotMax * 4 ))" ]; then
                    continue # less than 4 slices is too few
                fi
                slotSizeMax=$(( slotSize + 1 ))
                for isMapl in 1 0; do
                    #echo slotMax=$slotMax valSize=$valSize slotSize=$slotSize testType=$testType isMapl=$isMapl
                    cmd="${prog}_${testType} -i $iter -m $isMapl -p $testType -s $slotMax -v $valSize -S $slotSize -M $slotSizeMax"
                    echo "$cmd" | tee -a "${logfile}"
                    if [ "$dryrun" != "1" ] ; then
                        eval "$cmd" | tee -a "${logfile}"
                    fi

                    if [[ "$testType" == "rebalance" || "$testType" == "maplize" ]]; then
                        # no need to test isMapl is 0 case for rebalanceor maplize
                        break
                    fi
                done # isMapl
            done # slotSize
        done # slotMax
    done # valSize
done # testType
echo "$logfile"
