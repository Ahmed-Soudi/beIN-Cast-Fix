#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SDK_ROOT=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
if [[ -z "$SDK_ROOT" ]]; then
    echo "Set ANDROID_SDK_ROOT to an Android SDK installation." >&2
    exit 1
fi
NDK_DIR=${BEIN_NDK_DIR:-"$SDK_ROOT/ndk/27.3.13750724"}
CMAKE_BIN=${BEIN_CMAKE_BIN:-"$SDK_ROOT/cmake/3.22.1/bin/cmake"}
NINJA_BIN="$SDK_ROOT/cmake/3.22.1/bin/ninja"
ANDROID_JAR="$SDK_ROOT/platforms/android-34/android.jar"
D8_BIN="$SDK_ROOT/build-tools/35.0.0/d8"

for required in "$ANDROID_JAR" "$D8_BIN" "$CMAKE_BIN" "$NDK_DIR/build/cmake/android.toolchain.cmake"; do
    [[ -f "$required" ]] || { echo "Required tool missing: $required" >&2; exit 1; }
done

fetch_repo() {
    local url=$1 revision=$2 destination=$3
    if [[ ! -d "$destination/.git" ]]; then
        git init -q "$destination"
        git -C "$destination" remote add origin "$url"
    fi
    git -C "$destination" remote set-url origin "$url"
    git -C "$destination" fetch -q --depth 1 origin "$revision"
    git -C "$destination" checkout -q --detach FETCH_HEAD
    [[ $(git -C "$destination" rev-parse HEAD) == "$revision" ]] || exit 1
}

mkdir -p "$PROJECT_DIR/vendor" "$PROJECT_DIR/build/classes" "$PROJECT_DIR/build/dex"
fetch_repo https://github.com/LSPosed/LSPlant.git a612522188d903a523fc6760cd4ee257c3224d8c "$PROJECT_DIR/vendor/lsplant"
git -C "$PROJECT_DIR/vendor/lsplant" submodule update --init --recursive --depth 1
python3 - "$PROJECT_DIR/vendor/lsplant" <<'PY'
import subprocess, sys
state = subprocess.check_output(['git', '-C', sys.argv[1], 'submodule', 'status', '--recursive'], text=True)
for expected in ['b5a00f2ea94ad4c3b92054fd53896dbb429298f9', 'c2fabc9ac008c4ce8ef86e8c477ee3ea15cb2ab2']:
    if expected not in state:
        raise SystemExit('Pinned nested dependency missing: ' + expected)
if any(line.startswith(('-', '+', 'U')) for line in state.splitlines()):
    raise SystemExit('Nested dependency checkout differs from pinned source')
PY
fetch_repo https://github.com/LSPosed/Dobby.git 6813ca76ddeafcaece525bf8c6cde7ff4c21d3ce "$PROJECT_DIR/vendor/dobby"
fetch_repo https://github.com/topjohnwu/zygisk-module-sample.git 7bb941ac8edfcffd1d23761e401c45ca95409dc1 "$PROJECT_DIR/vendor/zygisk"
fetch_repo https://github.com/tukaani-project/xz-embedded.git ae63ae3a36ed01724674e8f3d750dc47bf125410 "$PROJECT_DIR/vendor/xz-embedded"

bash "$PROJECT_DIR/tests/run-art-resolver-tests.sh"
bash "$PROJECT_DIR/tests/run-host-tests.sh"
javac --release 8 -cp "$ANDROID_JAR" -d "$PROJECT_DIR/build/classes" \
    "$PROJECT_DIR/java/com/soudi/beincastroot/HookBridge.java"
"$D8_BIN" --release --min-api 26 --lib "$ANDROID_JAR" \
    --output "$PROJECT_DIR/build/dex" \
    "$PROJECT_DIR/build/classes/com/soudi/beincastroot/HookBridge.class"

STAGE_DIR="$PROJECT_DIR/build/module"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/zygisk"
cp -a "$PROJECT_DIR/module/." "$STAGE_DIR/"
cp "$PROJECT_DIR/build/dex/classes.dex" "$STAGE_DIR/hook.dex"
cp "$PROJECT_DIR/README.md" "$STAGE_DIR/README.md"
cp "$PROJECT_DIR/THIRD_PARTY.md" "$STAGE_DIR/THIRD_PARTY.md"
cp "$PROJECT_DIR/LICENSE" "$STAGE_DIR/LICENSE"
python3 - "$PROJECT_DIR/vendor" "$STAGE_DIR/licenses" <<'PY'
from pathlib import Path
import sys
vendor, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
for name in ['lsplant', 'dobby', 'xz-embedded']:
    source = vendor / name
    files = [p for p in source.iterdir() if p.is_file() and p.name.upper().startswith(('LICENSE', 'COPYING'))]
    if not files:
        raise SystemExit('Missing upstream license: ' + name)
    for path in files:
        (out / (name + '-' + path.name)).write_bytes(path.read_bytes())
for source in (vendor / 'lsplant').rglob('*'):
    if source.is_file() and source.name.upper().startswith(('LICENSE', 'COPYING')) and '.git' not in source.parts:
        relative = source.relative_to(vendor / 'lsplant').as_posix().replace('/', '-')
        (out / ('lsplant-' + relative)).write_bytes(source.read_bytes())
(out / 'zygisk-api-header.txt').write_bytes((vendor / 'zygisk/module/jni/zygisk.hpp').read_bytes())
PY

for abi in arm64-v8a armeabi-v7a; do
    NATIVE_BUILD_DIR="$PROJECT_DIR/build/$abi"
    "$CMAKE_BIN" -S "$PROJECT_DIR/native" -B "$NATIVE_BUILD_DIR" -G Ninja \
        -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
        -DCMAKE_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-28 \
        -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
    "$CMAKE_BIN" --build "$NATIVE_BUILD_DIR" --parallel 2
    READELF_BIN="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
    python3 - "$READELF_BIN" "$NATIVE_BUILD_DIR/libbein_cast_root.so" <<'PY'
import subprocess, sys
symbols = subprocess.check_output([sys.argv[1], '--dyn-syms', '--wide', sys.argv[2]], text=True)
if 'zygisk_module_entry' not in symbols:
    raise SystemExit('Missing Zygisk entry export')
dependencies = subprocess.check_output([sys.argv[1], '--dynamic', '--wide', sys.argv[2]], text=True)
for library in ['libc++_shared.so', 'liblsplant', 'libdobby', 'liblzma']:
    if library in dependencies:
        raise SystemExit('Unexpected shared-library dependency: ' + library)
PY
    cp "$NATIVE_BUILD_DIR/libbein_cast_root.so" "$STAGE_DIR/zygisk/$abi.so"
done

mkdir -p "$PROJECT_DIR/dist"
python3 "$PROJECT_DIR/package.py" "$STAGE_DIR" "$PROJECT_DIR/dist/beIN-Cast-Root-v5-prototype.zip"
