#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
XZ_ROOT="$PROJECT_DIR/vendor/xz-embedded"
TEST_BUILD_DIR="$PROJECT_DIR/build/art-compatibility-tests"
mkdir -p "$TEST_BUILD_DIR"
XZ_DEFINES=(-DXZ_DEC_SINGLE -DXZ_USE_CRC64 -DXZ_USE_SHA256 -DXZ_DEC_X86 -DXZ_DEC_ARM -DXZ_DEC_ARMTHUMB -DXZ_DEC_ARM64)
XZ_INCLUDES=(-I"$XZ_ROOT/linux/include/linux" -I"$XZ_ROOT/userspace" -I"$XZ_ROOT/linux/lib/xz")
for source in xz_crc32 xz_crc64 xz_sha256 xz_dec_stream xz_dec_lzma2 xz_dec_bcj; do
    cc -std=c99 -O2 -Wall -Wextra "${XZ_DEFINES[@]}" "${XZ_INCLUDES[@]}" \
        -c "$XZ_ROOT/linux/lib/xz/$source.c" -o "$TEST_BUILD_DIR/$source.o"
done
g++ -std=c++20 -O2 -Wall -Wextra -Wformat-security -Werror "${XZ_DEFINES[@]}" "${XZ_INCLUDES[@]}" \
    -I"$PROJECT_DIR/native" "$PROJECT_DIR/tests/art-compatibility-test.cpp" \
    "$TEST_BUILD_DIR"/*.o -ldl -o "$TEST_BUILD_DIR/art-compatibility-test"
# The optional private device ELF stays outside the repository and package.
"$TEST_BUILD_DIR/art-compatibility-test" "$@"
