The native library uses LSPlant directly, without the LSPosed framework, service,
module loader, manager, legacy API, or modern API. Exact upstream commits and
expected directories are in `dependencies.json`.

Use Git to check out each exact commit. Initialize LSPlant's submodules recursively,
and verify the DexBuilder and parallel-hashmap commits listed in the manifest.
GitHub source archives do not contain submodule contents. Source-download or build
failures must stop the build; do not substitute a moving branch or another binary.
After checking out Dobby, run `python3 native/patches/apply-dobby-patch.py vendor/dobby`
before configuring CMake. The recorded patch corrects its Android ARM64 ELF
relocations and ARM32 assembler flags, with exact upstream-file hash checks.

The LSPlant revision is the revision pinned by public LSPosed source in January
2024. Its CMake project uses C++20, rather than the current LSPlant C++ modules.
Dobby is pinned to a source revision also used by the standalone Zygisk example
https://github.com/tiwe0/Dejavu/blob/main/zygisk/scripts/fetch-third-party.sh .
These are source references, not proof of operation on this device.

Build with an Android NDK supporting C++20 (the project build uses NDK r27), CMake
3.22 or later, Android API 26, and `ANDROID_STL=c++_static`. Build each ABI separately:
`arm64-v8a` and `armeabi-v7a`. The library is `libbein_cast_root.so`; package it as
`zygisk/<ABI>.so`. Its only public export is `zygisk_module_entry`. Check that
`libc++_shared.so`, LSPlant, and Dobby are absent from ELF DT_NEEDED entries. Static
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
