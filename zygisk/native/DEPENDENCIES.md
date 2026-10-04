The native library uses LSPlant directly, without the LSPosed framework, service,
module loader, manager, legacy API, or modern API. Exact upstream commits and
expected directories are in `dependencies.json`.

Use Git to check out each exact commit. Initialize LSPlant's submodules recursively,
and verify the DexBuilder and parallel-hashmap commits listed in the manifest.
GitHub source archives do not contain submodule contents. Source-download or build
failures must stop the build; do not substitute a moving branch or another binary.

LSPlant and Dobby are the exact pair pinned by the public LSPosed tree at
`df74d83eb03a44cc6ad268841ac2ada28d077c77` (January 2024):
https://github.com/LSPosed/LSPosed/tree/df74d83eb03a44cc6ad268841ac2ada28d077c77/external .
LSPlant uses C++20, rather than the current LSPlant C++ modules. The Dobby fork
uses C++ generated ARM bridges and exposes a static `dobby` CMake target when
`DOBBY_GENERATE_SHARED=OFF`, as configured in that LSPosed source tree. Dobby has
no recursive submodules. These source references do not prove operation on this
device; the project NDK build and subsequent device test provide that validation.

Version 7 retains the checked ClassLinker and hook-helper backport in `../patches/`
to the pinned LSPlant headers before compiling. The source reference is upstream
commit `0cb2a316f7b1a8f2f5bbccd6ae56fdf0fc209081`, linked by the maintainer in
Samsung issue #171. That report concerns Android 16; this module remains gated
to API 33 and uses only the alternatives already present in its pinned source.
The checked backport requires a genuinely installed Fixup or supported ARM
visibility hook and preserves the registration and interpreter requirements.
An inline backend returning no backup is treated as failure. The patch script,
diff, and source-hash checks are provided with the module for rebuilding.

The device's actual ART binary also requires the exact JIT `DoCollection` alternative
from upstream commit `0d9faca38da7023fa39fc93383051a04458a3518`. The checked patch
preserves method migration and required collector coverage. A separate local
`UpdateMethodsCodeImpl` hook protects release-runtime entrypoint writes observed
in this binary, and an early ArtMethod size check rejects an unexpected JNI probe.
These local adjustments are restricted by the module to the inspected ARM64 GNU
build ID; the OS SDK alone is insufficient. Preflight checks mandatory release
symbols before callback loading or native hooks. Unknown builds and debuggable
runtimes are skipped. See `ART-PROFILE.md` for evidence and device-test limits.

XZ Embedded is pinned at `ae63ae3a36ed01724674e8f3d750dc47bf125410` (0BSD).
The static decoder supports CRC32, CRC64, SHA-256, and the ARM, ARM64, ARM Thumb,
and x86 BCJ filters; unsupported integrity checks are rejected. It is used only
to decode the loaded runtime's `.gnu_debugdata` into a bounded mini-ELF symbol
table. The resolver validates ELF layout and resolved addresses against the
original runtime's loaded segments. No runtime library is replaced and no
undocumented system `liblzma` dependency is introduced.

Build with an Android NDK supporting C++20 (the project build uses NDK r27), CMake
3.22 or later, Android API 26, and `ANDROID_STL=c++_static`. Build each ABI separately:
`arm64-v8a` and `armeabi-v7a`. The library is `libbein_cast_root.so`; package it as
`zygisk/<ABI>.so`. Its only public export is `zygisk_module_entry`. Check that
`libc++_shared.so`, LSPlant, Dobby, and `liblzma` are absent from ELF DT_NEEDED entries. Static
library symbols are hidden to reduce C++ symbol collisions with the host app.

Package the compiled Java bridge as `hook.dex` at the module root. The native
library reads it before sandbox specialization, only in `ptv.bein.mena`, and loads
it from memory afterward. This prototype intentionally runs only on Android 13
(API 33). Nonmatching processes and Android versions do not initialize ART hooks.

Retain upstream licenses and provide the pinned source and build instructions
with any distributed binary. LSPlant and DexBuilder use LGPL-3.0; static linking
requires making the relevant object/source material available for relinking.
Rebuilding from the pinned source is part of the supplied prototype workflow.

This remains a device experiment. LSPlant changes ART and generates hook stub DEX
classes; removing the LSPosed loader does not establish that beIN accepts this
different instrumentation. Neither authentication, DRM, receiver selection, nor
root checks are modified by this code.
