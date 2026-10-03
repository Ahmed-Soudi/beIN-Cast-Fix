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
