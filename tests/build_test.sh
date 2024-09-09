#!/usr/bin/env bash

# build multiple build types (Debug & Release) and make sure they all pass

ScriptDir="$(dirname "$(realpath "$0")")"
PrjDir="$(dirname "$ScriptDir")"
OutFile=/tmp/tlx_build_test_$$.txt

function RunBuild()
{
    BuildType=$1
    if ! cmake -DTLX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=$BuildType "$PrjDir" >> "$OutFile" 2>&1 ; then
	return 1
    fi
    if ! cmake --build . -j >> "$OutFile" 2>&1 ; then
	return 1
    fi
    return 0
}

echo "Output in file $OutFile"
prev_length=0
for BuildType in Debug Release
do
    printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
    BuildDir="$PrjDir/build/$BuildType"
    echo "Building $BuildType in $BuildDir"
    mkdir -p "$BuildDir"
    cd "$BuildDir"
    RunBuild $BuildType &

    cmake_pid=$!

    while kill -0 $cmake_pid 2> /dev/null; do
	last_line=$(tail -n 1 "$OutFile")

	# Calculate the length of the current line
	current_length=${#last_line}

	# If the current line is shorter than the previous one, append spaces
	if [ $current_length -lt $prev_length ]; then
            padding=$(printf '%*s' $((prev_length - current_length)) '')
            last_line="${last_line}${padding}"
	fi

	printf '\r%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
	echo -ne "$last_line"

	# Update the previous line length
	prev_length=$current_length

	sleep 1
    done

    # Wait for the cmake command to finish and capture its exit status
    wait $cmake_pid
    cmake_status=$?

    echo ""

    # Check if make command was successful
    if [ $cmake_status -ne 0 ]; then
	echo "cmake command failed with $cmake_status"
	exit 1
    fi
    printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
    echo "[PASS] Built $BuildType"
done

printf '%02d:%02d: ' "$(( SECONDS/60 ))" "$(( SECONDS%60 ))"
echo "[PASS] All"
