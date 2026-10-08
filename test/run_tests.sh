#!/usr/bin/env bash
# Builds and runs every host test. Exit code is non-zero if any test fails.
set -u

cd "$(dirname "$0")"
CXX="${CXX:-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "No C++ compiler found: $CXX"
    echo "Install one (e.g. 'sudo apt install g++') or set CXX to its path."
    exit 1
fi
FLAGS="-std=c++17 -Wall -Wextra -Wshadow -Wstringop-truncation -O1 -Istubs"
OUT="build"
mkdir -p "$OUT"

failed=0
for src in test_*.cpp; do
    name="${src%.cpp}"
    if ! $CXX $FLAGS "$src" ../src/SimcomA76xx.cpp -o "$OUT/$name" -lpthread; then
        echo "BUILD FAILED: $src"
        failed=1
        continue
    fi
    if ! "./$OUT/$name"; then
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    echo "=== TESTS FAILED ==="
else
    echo "=== ALL TESTS PASSED ==="
fi
exit $failed
