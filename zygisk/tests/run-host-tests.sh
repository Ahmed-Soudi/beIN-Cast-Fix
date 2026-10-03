#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TEST_CLASSES="$PROJECT_DIR/build/host-tests"
mkdir -p "$TEST_CLASSES"
javac --release 8 -d "$TEST_CLASSES" \
    "$PROJECT_DIR/tests/android/content/Context.java" \
    "$PROJECT_DIR/tests/android/util/Log.java" \
    "$PROJECT_DIR/java/com/soudi/beincastroot/HookBridge.java" \
    "$PROJECT_DIR/tests/CallbackTest.java"
java -cp "$TEST_CLASSES" CallbackTest
