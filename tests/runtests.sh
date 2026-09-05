#!/bin/bash
# Rebuild and run the end-to-end tests.
set -e
cd "$(dirname "$0")/.."

cmake --build build >/dev/null || { echo "ERROR: build failed"; exit 1; }

for test in tests/test_*.py; do
    echo "== $test =="
    python3 "$test"
done

# resets the browser's remembered directory for a clean next session
rm -f ~/.config/jack_the_slicer/config