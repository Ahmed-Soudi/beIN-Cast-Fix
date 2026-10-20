#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TEST_BUILD_DIR="$PROJECT_DIR/build/native-hook-probe-tests"
mkdir -p "$TEST_BUILD_DIR"
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I"$PROJECT_DIR/native" \
    "$PROJECT_DIR/tests/native-hook-probe-test.cpp" -o "$TEST_BUILD_DIR/native-hook-probe-test"
"$TEST_BUILD_DIR/native-hook-probe-test"
