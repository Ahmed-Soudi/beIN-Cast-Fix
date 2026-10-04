# Inspected ART profile used by versions 7 and 8

This is a device-specific compatibility profile, not a general Android 13 ABI.
Only API 33, ARM64, GNU build ID `a47994c420371ffbd05d16fc9a15ac9f`, and a
non-Java-debuggable runtime are accepted. Identity and mandatory symbol checks
run before callback DEX loading or LSPlant initialization. An unknown identity
or failed check skips the experiment. The uploaded library and raw logs are
not included in the repository or module.

## Binary identity and symbol requirements

The inspected original ELF is 15,008,832 bytes, AArch64 ET_DYN, SHA-256
`c6652beac3bef5db6ce0b7c644887b880aa73046c290af001234ce9328854b56`.
Its mini-debug section is 313,768 compressed bytes, decoding to 2,048,048 bytes
and 16,708 full symbols, matching the v5/v6 device diagnostics.
The Android ELF note contains build SDK 37. That identifies a build setting;
it does not establish the device OS or the exact AOSP source revision.

The old `JitCodeCache::GarbageCollectCache(Thread*)` is absent. The exact
`DoCollection(Thread*)` symbol exists at relative address `0x86d38c`, size
1,936 bytes. `MoveObsoleteMethod(ArtMethod*, ArtMethod*)` exists as required.
The checked JIT patch follows upstream commit
[0d9faca38](https://github.com/LSPosed/LSPlant/commit/0d9faca38da7023fa39fc93383051a04458a3518),
preserving obsolete-method migration before calling the original collector.

The required release-runtime groups and alternatives are encoded in
[ArtCompatibility.hpp](ArtCompatibility.hpp), using exact symbol lookup except
for the same GetMethodShorty prefix already used by pinned LSPlant. Preflight
does not install hooks or execute the resolved functions. Optional symbols and
debug-only hooks are not treated as mandatory release requirements. Debuggable
runtimes are explicitly rejected because their extra legacy hooks are absent.

## Class visibility and entrypoint writes

The uploaded binary has initialized status 14 and visibly initialized status 15.
Both pinned and current upstream LSPlant label status 15 as initialized. However,
changing that constant to 14 would not cover this binary's immediate-visible
path. The retained status-15 capture precedes writes in both observed paths:

| Observed code | Evidence in the inspected binary |
| --- | --- |
| `ClassLinker::MarkClassInitialized` | Passes 14 to Class::SetStatus at `0x218994`/call `0x2189a0`. |
| Immediate-visible branch | Passes 15 at `0x218d64`/call `0x218d70`, then calls UpdateMethodsCodeImpl at `0x2191a0`. |
| `MarkVisiblyInitialized` | Passes 15 at `0x3a962c`/call `0x3a9638`, then updates entrypoints via CAS at `0x3a9b04`/call `0x3a9b0c`. |

The shared PLT target was checked against `.rela.plt` and resolves to the exact
Class::SetStatus symbol. Its handle pointer, uint8 status, and Thread pointer
calling convention agree with the existing adapter.

The visibility callback already restores backed-up methods after its original
call. The immediate-visible path instead needs protection at the exact
`Instrumentation::UpdateMethodsCodeImpl(ArtMethod*, const void*)` member function,
present at `0x84e910`, size 272. Version 7 requires installing that additional
release-mode hook. It preserves deoptimized methods and routes updates of hooked
methods to their backups, using the existing pinned instrumentation semantics.
Unhooked methods call the original with unchanged arguments. This local change
is restricted by the module's exact profile gate; it is not attributed to the
upstream JIT patch.

The member declaration is corroborated by
[LineageOS's ART instrumentation header](https://github.com/LineageOS/android_art/blob/lineage-23.2/runtime/instrumentation.h)
(inspected blob `5112f0e789faaba1e3eae281333c114a5f6a3d4d`). The uploaded binary's
observed argument registers and void-return behavior agree with that declaration.

## Layout and validation limits

Observed ArtMethod code uses a 32-byte object, access flags at +4, data at +16,
and entrypoint at +24. The runtime JNI probe must measure 32 bytes before the
engine calculates offsets or scans memory. The Runtime debug-state setter stores
within the existing 4,096-byte probe, at +980. Thread discovery uses the existing
supported accessor. No OS SDK override or guessed runtime offset is added.

Host checks use the production resolver against real PT_LOAD ranges from the
uploaded binary without executing ARM code. Generated fixtures check identity,
symbol alternatives, missing requirements, and malformed inputs. These establish
lookup and adapter behavior; they do not validate actual JNI ordering, Dobby
installation, Android concurrency, protected-app startup, or Cast playback.

In v6, partial native hooks remained after the later JIT initialization failure.
The Java OOM then repeated the inert LSPosed startup stack. Since no Cast hook
or hooked-method maps were installed, the class-status difference alone cannot
explain that crash. Its mechanism remains unproved. Preflight avoids known
missing-symbol failures before mutation; it does not roll back a later backend
or JNI initialization failure.

In v7, the engine and Java hooks completed initialization, but no successful Cast setter
override was logged. Startup again failed with the same protected Java stack as
the inert scoped tests, attempting a 725,122,424-byte allocation despite low live
heap usage. This does not prove whether runtime mutation or instrumentation
detection caused it. Version 8 retains this engine unchanged and compares a
resident-library control with read-only preflight against the same preflight
plus LSPlant initialization. Neither diagnostic stage loads the callback DEX,
deoptimizes Java methods, hooks Application.attach, or looks up Cast classes.
