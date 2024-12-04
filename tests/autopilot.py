#!/usr/bin/env python3

# run full set of experiments for the paper
# for all chosen value sizes
#    find the best parameters
#    run all tests with the best parameters

import argparse
import filecmp
import hashlib
import math
import os
import platform
import re
import shutil
import subprocess
import sys
import time
import sys
from datetime import timedelta

gOutDir = '.'
gIteration = 25 * 1000 * 1000

gCommonRepeat = 0.1
gScanRepeat = 0.01 # scan is too slow, repeat less

gStartTime = time.time()
gScriptDir = os.path.dirname(__file__)

gTitle = None

LINUX = 0
MACOS = 1

gCpuPerNuma = 0
gOS = None

gBalanced = {
    'insertProp': 25,
    'lookupProp': 25,
    'scanProp': 25,
    'scanLen': 100
}

gAllInsert = {
    'insertProp': 100,
    'lookupProp': 0,
    'scanProp': 0,
    'scanLen': 0
}

# YCSB-C
gAllLookup = {
    'insertProp': 0,
    'lookupProp': 100,
    'scanProp': 0,
    'scanLen': 0
}

gAllScan = {
    'insertProp': 0,
    'lookupProp': 0,
    'scanProp': 100,
    'scanLen': 100
}

gYcsbA = {
    'insertProp': 50,
    'lookupProp': 50,
    'scanProp': 0,
    'scanLen': 0
}

gYcsbB = {
    'insertProp': 5,
    'lookupProp': 95,
    'scanProp': 0,
    'scanLen': 0
}

gYcsbE = {
    'insertProp': 5,
    'lookupProp': 0,
    'scanProp': 95,
    'scanLen': 100
}

gAllWorkloads = [gBalanced, gAllInsert, gAllLookup, gAllScan, gYcsbA, gYcsbB, gYcsbE]

def formatTime(seconds):
    """Convert seconds to hh:mm:ss format."""
    hours = seconds // 3600
    minutes = (seconds % 3600) // 60
    seconds = seconds % 60
    return f"{hours:02}:{minutes:02}:{seconds:02}"

def prt(*args, **kwargs):
    elapsedTime = int(time.time() - gStartTime)
    print(formatTime(elapsedTime), *args, **kwargs)

def prtProgress(total, cur, testStartTime, prevLineLen, paramStr):
    """Print the progress of the test."""
    elapsedTime = int(time.time() - testStartTime)  # Calculate elapsed time
    if cur > 0:
        avgTimePerTest = elapsedTime / cur  # Calculate average time per test
        projectedTotalTime = avgTimePerTest * total
        remainingTime = projectedTotalTime - elapsedTime
    else:
        remainingTime = 0

    progressPercentage = (cur / total) * 100 if total > 0 else 0
    elapsedTimeStr = formatTime(elapsedTime)
    remainingTimeStr = formatTime(int(remainingTime))

    # Construct the progress line
    progressLine = (f"Progress: {cur}/{total} {progressPercentage:.2f}% | "
                    f"Elapsed: {elapsedTimeStr} | "
                    f"Remaining: {remainingTimeStr} "
                    )
    progressLine += paramStr

    # Clear the previous line and print the new progress line
    spacesToClear = " " * max(0, prevLineLen - len(progressLine))
    print(f"\r{progressLine}{spacesToClear}", end="", flush=True)

    return len(progressLine)

def convert(value):
    try:
        # First, try to convert to int
        return int(value)
    except ValueError:
        try:
            # If that fails, try to convert to float
            f = float(value)
            if math.isnan(f):
                f = 0.0
            return f
        except ValueError:
            # If all conversions fail, return the original value
            return value

def copyIfDifferent(newName, oldName):
    """
    Copy the file newName to oldName if their contents are different.

    Parameters:
        newName (str): Path to the source file.
        oldName (str): Path to the destination file.
    """
    try:
        # Check if oldName exists and compare contents
        if not filecmp.cmp(newName, oldName, shallow=False):
            # Copy newName to oldName
            shutil.copy2(newName, oldName)
            #print(f"Copied {newName} to {oldName} because contents differ.")
    except FileNotFoundError:
        # If oldName does not exist, copy newName to oldName
        shutil.copy2(newName, oldName)
        #print(f"Copied {newName} to {oldName} which does not exist.")

def getCpuInfo():
    global gCpuPerNuma
    osName = platform.system()
    if osName == "Darwin":
        gOS = MACOS
        gCpuPerNuma = os.cpu_count()
    elif osName == "Linux":
        gOS = LINUX
        cmd = "numactl --hardware | awk '/node 0 cpus:/ {print NF-3}'"
        rc, out = runCmd(cmd)
        if rc != 0:
            print(f"Command failed to get CPU per NUMA node:\n{cmd}")
            sys.exit(1)
        gCpuPerNuma = int(out)
    else:
        raise Exception(f"Unknown OS {osName}")

def genAllRunOpt(valSize, findBest=True,
                 bestBtreeParam=None, bestMapleParam=None):
    runParams = []
    compileParams = []
    seenCompileParams = set()
    dist = 'uniform'
    earlyUnlock = 0
    tryLock = 0
    if findBest:
        allThreads = [gCpuPerNuma]
        opProps = [gBalanced]
        allSlotMax = [16384, 8192, 4096, 2048, 1024, \
                      512, 256, 128, 64, 32, 16]
        allSliceSizes = []
        sliceSize = 32
        while sliceSize <= 128:
            allSliceSizes.append(sliceSize)
            sliceSize *= 2
    else:
        allThreads = [1, 2]
        for th in range(4, gCpuPerNuma + 1, 4):
            allThreads.append(th)
        opProps = gAllWorkloads

    for opProp in opProps:
        if opProp['scanProp'] > 0:
            repeat = gScanRepeat
        else:
            repeat = gCommonRepeat
        for maplizeThreshold in [0, 100]:
            if not findBest:
                if maplizeThreshold == 0:
                    allSlotMax = [bestMapleParam['SlotMax']]
                else:
                    allSlotMax = [bestBtreeParam['SlotMax']]
            for slotMax in allSlotMax:
                if findBest:
                    if slotMax == 64 or maplizeThreshold == 100:
                        allInnerMax = [64] # B-tree always use slotMax 64
                    else:
                        allInnerMax = [64, slotMax]
                else:
                    if maplizeThreshold == 0:
                        allInnerMax = [bestMapleParam['InnSlot']]
                    else:
                        allInnerMax = [bestBtreeParam['InnSlot']]
                for innerMax in allInnerMax:
                    sliceSize = 32
                    if not findBest:
                        if maplizeThreshold == 0:
                            allSliceSizes = [bestMapleParam['SliceSz']]
                        else:
                            allSliceSizes = [bestBtreeParam['SliceSz']]
                    for sliceSize in allSliceSizes:
                        sliceSizeMax = sliceSize + 1
                        for thread in allThreads:
                            param = {}
                            param['check-only'] = 0
                            param['test'] = 'btreemix'
                            param['slot-max'] = slotMax
                            param['inner-max'] = innerMax
                            param['val-size'] = valSize
                            param['slice-size'] = sliceSize
                            param['slice-size-max'] = sliceSizeMax
                            param['early-unlock'] = earlyUnlock
                            param['iteration'] = gIteration
                            param['num-threads'] = thread
                            param['maplize-threshhold'] = maplizeThreshold
                            param['insert-prop'] = opProp['insertProp']
                            param['lookup-prop'] = opProp['lookupProp']
                            param['scan-prop'] = opProp['scanProp']
                            param['scan-len'] = opProp['scanLen']
                            param['try-lock'] = tryLock
                            param['dist'] = dist
                            param['repeats'] = repeat
                            runParams.append(param)

                            compileParam = {}
                            compileParam['slot-max'] = slotMax
                            compileParam['inner-max'] = innerMax
                            compileParam['val-size'] = valSize
                            compileParam['slice-size'] = sliceSize
                            compileParam['slice-size-max'] = sliceSizeMax

                            frozenItem = frozenset(compileParam.items())
                            if frozenItem not in seenCompileParams:
                                seenCompileParams.add(frozenItem)
                                compileParams.append(compileParam)

    return runParams, compileParams

def runCmd(cmd):
    result = subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, cwd=gScriptDir, shell=True)
    return result.returncode, result.stdout

def buildBtreeMixOpt(params):
    oldName = os.path.join(
        gScriptDir, 'container/btree_speedtest_btreemix_options.hpp')
    newName = '/tmp/btree_speedtest_btreemix_options.hpp'
    with open(newName, 'w') as f:
        for param in params:
            f.write(f'RUN_BTREEMIX(' +
                    f'{param["slot-max"]}, ' +
                    f'{param["inner-max"]}, ' +
                    f'{param["val-size"]}, ' +
                    f'{param["slice-size"]}, ' +
                    f'{param["slice-size-max"]});\n')

    copyIfDifferent(newName, oldName)

    prt('Build to find best param')
    rc, out = runCmd('./build_test.sh -b')
    if rc != 0:
        prt('Build failed with error:\n', out)
        sys.exit(1)

def runTests(params):
    allResults = []
    total = len(params)
    cur = 0
    testStartTime = time.time()
    prevLineLen = 0
    maxMops = -1
    for p in params:
        cur += 1
        if gOS == LINUX:
            cmd = 'numactl -N -0 -m 0 '
        else:
            cmd = ''
        cmd = '../build/Release/tests/tlx_container_btree_speedtest_btreemix '
        cmd += f'--check-only 0 '
        cmd += f'--test btreemix '
        cmd += f'--slot-max {p["slot-max"]} '
        cmd += f'--inner-max {p["inner-max"]} '
        cmd += f'--val-size {p["val-size"]} '
        cmd += f'--slice-size {p["slice-size"]} '
        cmd += f'--slice-size-max {p["slice-size-max"]} '
        cmd += f'--early-unlock {p["early-unlock"]} '
        cmd += f'--iteration {p["iteration"]} '
        cmd += f'--num-threads {p["num-threads"]} '
        cmd += f'--maplize-threshhold {p["maplize-threshhold"]} '
        cmd += f'--insert-prop {p["insert-prop"]} '
        cmd += f'--lookup-prop {p["lookup-prop"]} '
        cmd += f'--scan-prop {p["scan-prop"]} '
        cmd += f'--scan-len {p["scan-len"]} '
        cmd += f'--try-lock {p["try-lock"]} '
        cmd += f'--dist {p["dist"]} '
        cmd += f'--repeats {p["repeats"]}'

        paramStr = f'val={p["val-size"]} ' + \
            f'slots={p["slot-max"]} ' + \
            f'inner={p["inner-max"]} ' + \
            f'slice={p["slice-size"]} ' + \
            f'maxMops={maxMops}'
        prevLineLen = prtProgress(
            total, cur, testStartTime, prevLineLen, paramStr)

        rc, out = runCmd(cmd)
        if rc != 0:
            prt(f'Command failed with {rc}:\n' + cmd + "\n" + out)
            exit(1)
        lastLines = out.splitlines()[-2:]
        keys = lastLines[0].split('\t')
        vals = lastLines[1].split('\t')
        convVals = [convert(v) for v in vals]
        res = dict(zip(keys, convVals))
        global gTitle
        if gTitle is None:
            gTitle = lastLines[0]
        res['orig-result'] = lastLines[1]
        if maxMops < res['Mops']:
            maxMops = res['Mops']
        allResults.append(res)

    print('\n')

    return allResults

def findBestParam(results):
    bestMapleParam = {'Mops': -1}
    bestBtreeParam = {'Mops': -1}

    for res in results:
        if res['MplThrh'] == 0: # MAPLe
            if bestMapleParam['Mops'] < res['Mops']:
                bestMapleParam = res
        else:
            if bestBtreeParam['Mops'] < res['Mops']:
                bestBtreeParam = res
    return bestMapleParam, bestBtreeParam

def autoFindBestParam(valSize):
    runParams, compileParams = genAllRunOpt(valSize, findBest=True)
    buildBtreeMixOpt(compileParams)

    prt(f'val={valSize} find best param')
    results = runTests(runParams)
    bestMapleParam, bestBtreeParam = findBestParam(results)
    return bestMapleParam, bestBtreeParam

def printBestParam(valSize, bestMapleParam, bestBtreeParam):
    outNameBest = os.path.join(gScriptDir, gOutDir,
                               f'val-{valSize}-best-param.txt')
    with open(outNameBest, 'w') as f:
        f.write(gTitle + '\n')
        f.write(bestMapleParam['orig-result'] + '\n')
        f.write(bestBtreeParam['orig-result'] + '\n')
    prt(f'Best params in {outNameBest}')

def printResults(valSize, allResults):
    outNameAll = os.path.join(gScriptDir, gOutDir,
                              f'val-{valSize}-all.txt')
    with open(outNameAll, 'w') as f:
        f.write(gTitle + '\n')
        for res in allResults:
            f.write(res['orig-result'])
    prt(f'All rsults in {outNameAll}')

def autopilot(valSize):
    bestMapleParam, bestBtreeParam = autoFindBestParam(valSize)
    printBestParam(valSize, bestMapleParam, bestBtreeParam)

    runParams, compileParams = genAllRunOpt(
        valSize, findBest=False, bestBtreeParam = bestBtreeParam,
        bestMapleParam = bestMapleParam)

    prt(f'val={valSize} run all workloads with best param')
    results = runTests(runParams)

    printResults(valSize, results)

def main():
    getCpuInfo()

    parser = argparse.ArgumentParser(
        description="autopilot.py -v <valSize> and -d <dir> arguments.")

    parser.add_argument("-v", type=int, action="append",
                        help="Value sizes, can specify multiple times")
    parser.add_argument("-d", type=str, help="Output directory")

    args = parser.parse_args()

    gOutDir = args.d
    valSizes = args.v

    #valSizes = [128]

    for valSize in valSizes:
        autopilot(valSize)

if __name__ == "__main__":
    main()
