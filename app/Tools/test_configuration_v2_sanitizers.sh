#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build="$repo/artifacts/configuration_v2/host-tests-asan"
mkdir -p "$build"
options=(-DCMAKE_BUILD_TYPE=Debug -DCONFIGURATION_TEST_SANITIZERS=ON)
unity="$repo/artifacts/configuration_v2/host-tests/_deps/unity-src"
if [[ -f "$unity/CMakeLists.txt" ]]; then
    options+=("-DFETCHCONTENT_SOURCE_DIR_UNITY=$unity")
fi
cmake -S "$repo/app/Tests" -B "$build" "${options[@]}" 2>&1 | tee "$build/configure.log"
cmake --build "$build" -j 4 2>&1 | tee "$build/build.log"
ctest --test-dir "$build" --output-on-failure --verbose --output-junit "$build/ctest-results.xml" 2>&1 | tee "$build/ctest.log"
