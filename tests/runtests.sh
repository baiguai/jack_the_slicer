#!/bin/bash
# Rebuild and run the end-to-end file-browser tests.
set -e
cd "$(dirname "$0")/.."

cmake --build build >/dev/null
echo "== test_open_flow.py =="
python3 tests/test_open_flow.py