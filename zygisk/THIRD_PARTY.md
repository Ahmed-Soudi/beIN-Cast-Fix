# Source dependencies and notices

This module's source is published under GNU GPL version 3; see LICENSE. Source dependencies are fetched at fixed revisions during the build. No vendor source is bundled in this project checkout.

- **LSPlant**: https://github.com/LSPosed/LSPlant/tree/a612522188d903a523fc6760cd4ee257c3224d8c — LGPL-3.0. The static library and its generated hook stubs provide Java-method interception. This is a standalone dependency, not a source match for LSPosed build 7854.
- **DexBuilder**: https://github.com/LSPosed/DexBuilder/tree/b5a00f2ea94ad4c3b92054fd53896dbb429298f9 — LGPL-3.0; fetched through LSPlant's pinned submodule.
- **parallel-hashmap**: revision c2fabc9ac008c4ce8ef86e8c477ee3ea15cb2ab2 — fetched through the pinned DexBuilder submodule; its license is retained from that checkout.
- **Dobby**: https://github.com/jmpews/Dobby/tree/5dfc8546954ce3b3198132ab13fddb89ee92cdd7 — Apache-2.0 native inline-hook backend.
- **Official Zygisk module API header**: https://github.com/topjohnwu/zygisk-module-sample/tree/7bb941ac8edfcffd1d23761e401c45ca95409dc1 — 0BSD API version 4 header; copyright and permission notice remain in its source.
- **Magisk module installer**: https://github.com/topjohnwu/Magisk/blob/v28.1/scripts/module_installer.sh — GPL-3.0; standard installer entry point included under META-INF for Magisk installation. KernelSU uses its own module installer.

The corresponding source revisions and build scripts are provided so the statically linked native library can be rebuilt and modified. Dependency license files are included in the generated module ZIP under `licenses/`.

The pinned Dobby source receives the recorded `native/patches/dobby-android-elf.patch` before building: ARM64 assembly uses Android ELF relocation syntax while preserving the Apple branch, and an Apple-only `-arch armv7` flag is removed from Android ARM32 builds. The patch script verifies the source revision and exact input before applying it. This repairs upstream build portability; it does not change the Cast hook.
