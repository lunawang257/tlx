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
import signal
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

gFast = False

# 'Test\tSlotMax\tInnSlot\tValSize\tSliceSz\tSlcSzMx\tErlyULk\tTryLock\tThreads\tMplThrh\tSHght\tEHght\tDist\tInsertP\tLookupP\tScnP\tScnLen\tMops\tWaitPct\tMaplPct\tMaplRd%\tMaplWt%\tLfRLkns\tLfWLkns\tInRLkns\tInWLkns\tMaplTms\tMaplCnt\tUMplTms\tUMplCnt\titem(M)\trepeats\tDurtion\tRmCt\tRmLkPrt\tRmTryLk\tRmTrLk1\tRmSmPrt\tRm!UpKy\tR!Udflw'
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

gBalanced = {
    'insertProp': 15,
    'lookupProp': 50,
    'scanProp': 20,
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
        rc, out, _, _ = runCmd(cmd)
        if rc != 0:
            print(f"Command failed to get CPU per NUMA node:\n{cmd}")
            sys.exit(1)
        gCpuPerNuma = int(out)
    else:
        raise Exception(f"Unknown OS {osName}")

def makeParams(
        slotMax=None, innerMax=None, valSize=None,
        sliceSize=None, sliceSizeMax=None,
        earlyUnlock=None, iteration=None,
        thread=None, maplizeThreshold=None,
        opProp=None, tryLock=None, dist=None, repeat=None):
    param = {}
    param['check-only'] = 0
    param['test'] = 'btreemix'
    param['slot-max'] = slotMax
    param['inner-max'] = innerMax
    param['val-size'] = valSize
    param['slice-size'] = sliceSize
    param['slice-size-max'] = sliceSizeMax
    param['early-unlock'] = earlyUnlock
    param['iteration'] = iteration
    param['num-threads'] = thread
    param['maplize-threshhold'] = maplizeThreshold
    param['insert-prop'] = opProp['insertProp']
    param['lookup-prop'] = opProp['lookupProp']
    param['scan-prop'] = opProp['scanProp']
    param['scan-len'] = opProp['scanLen']
    param['try-lock'] = tryLock
    param['dist'] = dist
    param['repeats'] = repeat

    compileParam = {}
    compileParam['slot-max'] = slotMax
    compileParam['inner-max'] = innerMax
    compileParam['val-size'] = valSize
    compileParam['slice-size'] = sliceSize
    compileParam['slice-size-max'] = sliceSizeMax

    return param, compileParam

def addNewParam(compileParams, seenCompileParams, compileParam):
    frozenItem = frozenset(compileParam.items())
    if frozenItem not in seenCompileParams:
        seenCompileParams.add(frozenItem)
        compileParams.append(compileParam)

def addGuessedBestParam(runParams, compileParams, seenCompileParams, valSize):
    # MAPLe best param
    param, compileParam = makeParams(
        slotMax=8192, innerMax=8192,
        valSize=valSize, sliceSize=32,
        sliceSizeMax=33,
        earlyUnlock=0, iteration=gIteration,
        thread=gCpuPerNuma, maplizeThreshold=0,
        opProp=gBalanced, tryLock=0, dist='uniform',
        repeat=0.01)
    runParams.append(param)
    addNewParam(compileParams, seenCompileParams, compileParam)

    # 1-slice best param
    param, compileParam = makeParams(
        slotMax=4096, innerMax=4096,
        valSize=valSize, sliceSize=4096,
        sliceSizeMax=4097,
        earlyUnlock=0, iteration=gIteration,
        thread=gCpuPerNuma, maplizeThreshold=0,
        opProp=gBalanced, tryLock=0, dist='uniform',
        repeat=0.01)
    runParams.append(param)
    addNewParam(compileParams, seenCompileParams, compileParam)

    # B-tree best param
    param, compileParam = makeParams(
        slotMax=128, innerMax=64,
        valSize=valSize, sliceSize=32,
        sliceSizeMax=33,
        earlyUnlock=0, iteration=gIteration,
        thread=gCpuPerNuma, maplizeThreshold=100,
        opProp=gBalanced, tryLock=0, dist='uniform',
        repeat=0.01)
    runParams.append(param)
    addNewParam(compileParams, seenCompileParams, compileParam)

def genAllRunOpt(valSize, findBest=True,
                 bestBtreeParam=None, bestMapleParam=None, best1SliceParam=None):
    runParams = []
    compileParams = []
    seenCompileParams = set()

    # this can speed up other runs significantly
    if findBest:
        addGuessedBestParam(runParams, compileParams, seenCompileParams, valSize)

    earlyUnlock = 0
    tryLock = 0
    if findBest:
        allDist = ['uniform']
        allThreads = [gCpuPerNuma]
        opProps = [gBalanced]
        allSlotMax = [16384, 8192, 4096, 2048, 1024, \
                      512, 256, 128, 64, 32, 16]
        allSliceSizes = []
        sliceSize = 32
        while sliceSize <= 128:
            allSliceSizes.append(sliceSize)
            sliceSize *= 2
        allSliceSizes.append(-1) # means 1-slice
    else:
        allDist = ['uniform', 'zipf']
        opProps = gAllWorkloads
        if gFast:
            allThreads = [gCpuPerNuma]
        else:
            allThreads = [1, 2]
            for th in range(4, gCpuPerNuma + 1, 4):
                allThreads.append(th)

    for opProp in opProps:
        if opProp['scanProp'] > 0:
            repeat = gScanRepeat
        else:
            repeat = gCommonRepeat
        for dist in allDist:
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
                        if not findBest:
                            if maplizeThreshold == 0:
                                allSliceSizes = [bestMapleParam['SliceSz']]
                            else:
                                allSliceSizes = [bestBtreeParam['SliceSz']]
                        for slcSize in allSliceSizes:
                            sliceSize = slcSize
                            if sliceSize == -1:
                                sliceSize = slotMax # for 1-slice test
                            sliceSizeMax = sliceSize + 1
                            for thread in allThreads:
                                param, compileParam = makeParams(
                                    slotMax=slotMax, innerMax=innerMax,
                                    valSize=valSize, sliceSize=sliceSize,
                                    sliceSizeMax=sliceSizeMax,
                                    earlyUnlock=earlyUnlock, iteration=gIteration,
                                    thread=thread, maplizeThreshold=maplizeThreshold,
                                    opProp=opProp, tryLock=tryLock, dist=dist,
                                    repeat=repeat)

                                runParams.append(param)
                                addNewParam(compileParams, seenCompileParams,
                                            compileParam)

    if not findBest: # run balanced test for 1-slice
        opProp = gBalanced
        if opProp['scanProp'] > 0:
            repeat = gScanRepeat
        else:
            repeat = gCommonRepeat
        maplizeThreshold = 0
        slotMax = best1SliceParam['SlotMax']
        innerMax = best1SliceParam['InnSlot']
        sliceSize = best1SliceParam['SliceSz']
        assert sliceSize == slotMax # this is a 1-slice test
        sliceSizeMax = sliceSize + 1
        for dist in allDist:
            for thread in allThreads:
                param, compileParam = makeParams(
                    slotMax=slotMax, innerMax=innerMax,
                    valSize=valSize, sliceSize=sliceSize,
                    sliceSizeMax=sliceSizeMax,
                    earlyUnlock=earlyUnlock, iteration=gIteration,
                    thread=thread, maplizeThreshold=maplizeThreshold,
                    opProp=opProp, tryLock=tryLock, dist=dist,
                    repeat=repeat)

                runParams.append(param)
                addNewParam(compileParams, seenCompileParams, compileParam)

    return runParams, compileParams

def runCmd(cmd, timeout=None):
    timedOut = False
    rc = 0
    stdout = ''
    try:
        start = time.time()
        process = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, cwd=gScriptDir, shell=True,
            start_new_session=True)
        stdout, stderr = process.communicate(timeout=timeout)
        stop = time.time()
        rc = 0
    except subprocess.TimeoutExpired:
        timedOut = True
        os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        stdout, stderr = process.communicate()
        stop = time.time()
        rc = 0
    stdout += stderr
    assert timedOut or stdout != ""
    if rc != 0:
        raise Exception(f"Command failed with {rc}:\n{cmd}\n{stdout}")
    return rc, stdout, stop - start, timedOut

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
    rc, out, _, _ = runCmd('./build_test.sh -b')
    if rc != 0:
        prt('Build failed with error:\n', out)
        sys.exit(1)

def runTests(params, findBest=False):
    allResults = []
    total = len(params)
    cur = 0
    testStartTime = time.time()
    prevLineLen = 0
    maxMops = -1
    minRunTimes = {
        'btree': 7200,
        'maple': 7200,
        '1-slice': 7200,
    }
    numTimeOut = 0
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
            f'maxMops={maxMops:.1f} '

        if findBest:
            paramStr += \
                f'minRunTime={minRunTimes["btree"]:.1f}(bt), ' + \
                f'{minRunTimes["maple"]:.1f}(mapl), ' + \
                f'{minRunTimes["1-slice"]:.1f}(1-slc) ' + \
                f'numTimeOut={numTimeOut}'

        prevLineLen = prtProgress(
            total, cur, testStartTime, prevLineLen, paramStr)

        if p['maplize-threshhold'] == 100:
            minRunTime = minRunTimes['btree']
        elif p['slice-size'] == p['slot-max']:
            minRunTime = minRunTimes['1-slice']
        else:
            minRunTime = minRunTimes['maple']

        rc, out, runTime, timedOut = runCmd(cmd, timeout=minRunTime * 2)
        if timedOut:
            #print('')
            #prt(f'minRunTime={minRunTime * 2}, timed out: {cmd}')
            numTimeOut += 1
            continue

        if findBest: # update min runtime
            if p['maplize-threshhold'] == 100:
                if minRunTimes['btree'] > runTime:
                    minRunTimes['btree'] = runTime
            elif p['slice-size'] == p['slot-max']:
                if minRunTimes['1-slice'] > runTime:
                    minRunTimes['1-slice'] = runTime
            else:
                if minRunTimes['maple'] > runTime:
                    minRunTimes['maple'] = runTime

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
        res['orig-cmd'] = cmd
        res['orig-result'] = lastLines[1]
        if maxMops < res['Mops']:
            maxMops = res['Mops']
        allResults.append(res)

    print('\n')

    return allResults

def findBestParam(results):
    bestBtreeParam = {'Mops': -1}
    bestMapleParam = {'Mops': -1}
    best1SliceParam = {'Mops': -1}

    for res in results:
        if res['MplThrh'] == 0: # MAPLe
            if res['SliceSz'] == res['SlotMax']:
                if best1SliceParam['Mops'] < res['Mops']:
                    best1SliceParam = res
            else:
                if bestMapleParam['Mops'] < res['Mops']:
                    bestMapleParam = res
        else:
            if bestBtreeParam['Mops'] < res['Mops']:
                bestBtreeParam = res
    return bestBtreeParam, bestMapleParam, best1SliceParam

def autoFindBestParam(valSize):
    runParams, compileParams = genAllRunOpt(valSize, findBest=True)
    buildBtreeMixOpt(compileParams)

    prt(f'val={valSize} find best param')
    results = runTests(runParams, findBest=True)

    outName = os.path.join(gScriptDir, gOutDir,
                           f'val-{valSize}-find-best-raw')
    printResults(outName, results)

    bestBtreeParam, bestMapleParam, best1SliceParam = findBestParam(results)
    return bestBtreeParam, bestMapleParam, best1SliceParam

def printBestParam(valSize, bestBtreeParam, bestMapleParam, best1SliceParam):
    outNameBest = os.path.join(gScriptDir, gOutDir,
                               f'val-{valSize}-best-param.txt')
    with open(outNameBest, 'w') as f:
        f.write(gTitle + '\n')
        f.write(bestBtreeParam['orig-result'] + '\n')
        f.write(bestMapleParam['orig-result'] + '\n')
        f.write(best1SliceParam['orig-result'] + '\n')
    prt(f'Best params in {outNameBest}')

def printResults(outNameBase, allResults):
    resOutName = outNameBase + ".txt"
    cmdOutName = outNameBase + ".cmd"
    with open(resOutName, 'w') as f:
        f.write(gTitle + '\n')
        for res in allResults:
            f.write(res['orig-result'] + '\n')
    with open(cmdOutName, 'w') as f:
        f.write('Full Command Line\n')
        for res in allResults:
            f.write(res['orig-cmd'] + '\n')

def autopilot(valSize):
    bestBtreeParam, bestMapleParam, best1SliceParam = autoFindBestParam(valSize)
    printBestParam(valSize, bestBtreeParam, bestMapleParam, best1SliceParam)

    runParams, compileParams = genAllRunOpt(
        valSize, findBest=False, bestBtreeParam=bestBtreeParam,
        bestMapleParam=bestMapleParam, best1SliceParam=best1SliceParam)

    prt(f'val={valSize} run all workloads with best param')
    results = runTests(runParams)

    outNameAll = os.path.join(gScriptDir, gOutDir,
                              f'val-{valSize}-all')
    printResults(outNameAll, results)

def main():
    global gOutDir, gFast, gIteration

    getCpuInfo()

    parser = argparse.ArgumentParser(
        description="autopilot.py -v <valSize> and -d <dir> arguments.")

    parser.add_argument("-d", "--outdir", type=str, help="Output directory",
                        default=".")
    parser.add_argument("-f", "--fast", action='store_true',
                        help="If given, only run the maximum thread count")
    parser.add_argument("-i", "--iterations", type=int, help=f"Run Iterations",
                        default=gIteration)
    parser.add_argument("-v", "--valsize", type=int, action="append",
                        help="Value sizes, can specify multiple times")

    args = parser.parse_args()

    gOutDir = args.outdir
    gFast = args.fast
    gIteration = args.iterations
    valSizes = args.valsize

    #valSizes = [128]

    for valSize in valSizes:
        autopilot(valSize)

    outName = os.path.join(gScriptDir, gOutDir, f'val-')
    print(f'Output files are in {outName}*')

if __name__ == "__main__":
    main()
