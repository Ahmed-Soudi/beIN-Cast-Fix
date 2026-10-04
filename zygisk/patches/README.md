# LSPlant initialization compatibility backport

Applies only to LSPlant commit `a612522188d903a523fc6760cd4ee257c3224d8c`.
The manifest checks the complete SHA-256 hashes of both original and patched
headers. `apply-lsplant-compat.py` refuses a different source revision, checks
all files before writing, and accepts an already applied patch unchanged.
`lsplant-compat.patch` publishes the corresponding source diff.

The pinned ClassLinker initialization aborts when all three known
`FixupStaticTrampolines` symbols are absent. It therefore never tries its
existing ARM class-visibility hooks. The backport retains all Fixup variants
and requires at least one genuinely installed class-initialization hook.
When Fixup is unavailable, only ARM/ARM64 on API 30 or later may use the
existing `AdjustThreadVisibilityCounter` or `MarkVisiblyInitialized` hook.
If neither alternative installs successfully, initialization still fails.
Native registration/unregistration and interpreter-entrypoint requirements
remain in place. Logs name the initialization symbol actually installed.

The adapter also rejects a null backend backup for both ordinary and member
function hooks. An unresolved symbol or failed backend installation cannot
satisfy the coverage requirement.

Upstream references:

- [LSPlant commit 0cb2a316](https://github.com/LSPosed/LSPlant/commit/0cb2a316f7b1a8f2f5bbccd6ae56fdf0fc209081)
  stopped making absence of Fixup fatal on ARM while retaining visibility hooks.
- [Samsung report #171](https://github.com/LSPosed/LSPlant/issues/171)
  describes the missing symbols on **Android 16**; the maintainer links the
  above commit. This is supporting source history, not evidence that the
  report concerns the user's Android 13 device.
- [LSPlant commit c6cc93ae](https://github.com/LSPosed/LSPlant/commit/c6cc93ae7ede5376e92996a8f07f1dc98f7e8fdd)
  introduced the visibility path because Fixup may be inlined.

No unknown overloads, compiler clones, guessed symbols, or hardcoded ART
offsets are introduced. Host tests compile the actual patched adapter body
with minimal test interfaces and verify failed/successful backups. They do
not execute Android ART or establish successful casting on a device.
