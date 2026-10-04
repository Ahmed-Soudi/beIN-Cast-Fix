#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
LSPLANT_DIR=${1:-"$PROJECT_DIR/vendor/lsplant"}
TEST_BUILD_DIR="$PROJECT_DIR/build/compat-tests"
mkdir -p "$TEST_BUILD_DIR"
python3 "$PROJECT_DIR/tests/test-compat-patch.py" "$LSPLANT_DIR" "$TEST_BUILD_DIR"
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I"$TEST_BUILD_DIR" \
    "$PROJECT_DIR/tests/hook-helper-backend-test.cpp" -o "$TEST_BUILD_DIR/hook-helper-backend-test"
"$TEST_BUILD_DIR/hook-helper-backend-test"
