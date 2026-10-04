# LSPlant compatibility patches for the inspected ART profile

Applies only to LSPlant commit `a612522188d903a523fc6760cd4ee257c3224d8c`.
The manifest checks the complete SHA-256 hashes of all original and patched
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

The JIT backport preserves `MoveObsoleteMethod` before collection and accepts
the exact upstream `DoCollection(Thread*)` entrypoint when the old
`GarbageCollectCache(Thread*)` is absent. Collection remains mandatory and
the log names the entrypoint actually hooked. This is the upstream change
in commit `0d9faca3`, with the same honest backend-success check as above.

Two additional local checks apply only inside this module's verified profile:
API 33, ARM64, non-Java-debuggable runtime, and original ELF GNU build ID
`a47994c420371ffbd05d16fc9a15ac9f`. The module enforces that profile and runs
the required release-symbol preflight before invoking LSPlant. These local
patches must not be reused as generic SDK33 changes.

The measured ArtMethod spacing must be 32 bytes before offset subtraction or
field scanning begins. An unexpected spacing fails before native hooks are
installed. The profiled runtime also requires an exact
`Instrumentation::UpdateMethodsCodeImpl(ArtMethod*, const void*)` hook even
for release apps. It uses the existing pinned entrypoint-update policy:
skip deoptimized targets, redirect updates into a hooked method's backup
when the target's entrypoint differs, and pass other methods through.

The original class-status value 15 is preserved. In this binary,
`MarkClassInitialized` sets status 15 at `0x218d70` then calls
`UpdateMethodsCodeImpl` at `0x2191a0`; `MarkVisiblyInitialized` sets status 15
at `0x3a9638` then performs an inlined entrypoint update at `0x3a9b0c`.
The release updater hook covers the former writes; the existing visibility
hook restores entrypoints after the latter. Changing the capture status to
14 would miss the direct-visible initialization path. These observations
are specific to the inspected binary and do not prove compatibility with
other ART builds.

Upstream references:

- [LSPlant commit 0cb2a316](https://github.com/LSPosed/LSPlant/commit/0cb2a316f7b1a8f2f5bbccd6ae56fdf0fc209081)
  stopped making absence of Fixup fatal on ARM while retaining visibility hooks.
- [Samsung report #171](https://github.com/LSPosed/LSPlant/issues/171)
  describes the missing symbols on **Android 16**; the maintainer links the
  above commit. This is supporting source history, not evidence that the
  report concerns the user's Android 13 device.
- [LSPlant commit c6cc93ae](https://github.com/LSPosed/LSPlant/commit/c6cc93ae7ede5376e92996a8f07f1dc98f7e8fdd)
  introduced the visibility path because Fixup may be inlined.
- [LSPlant commit 0d9faca3](https://github.com/LSPosed/LSPlant/commit/0d9faca38da7023fa39fc93383051a04458a3518)
  adds the exact `DoCollection` alternative with migration before the original.
- [ART Instrumentation declaration](https://github.com/LineageOS/android_art/blob/lineage-23.2/runtime/instrumentation.h)
  declares `void UpdateMethodsCodeImpl(ArtMethod*, const void*)`; the private
  binary's register use confirms the member-call signature for this profile.

No unknown overloads, compiler clones, guessed symbols, or hardcoded ART
field offsets are introduced. `tests/run-compat-tests.sh` compiles the actual
patched adapter and JIT/Instrumentation headers with minimal host interfaces.
It checks failed/successful installation, migration before original collection,
entrypoint retargeting, deoptimization skips and ordinary-method pass-through.
It also verifies checked patch application, idempotence and revision refusal
without partial writes. `tests/run-art-compatibility-tests.sh` checks the
profile gates and required release groups; its optional private ELF argument
exercises the real resolver and original load-segment bounds. The private
binary is neither committed nor packaged. Host tests do not execute Android
ART or establish successful casting on a device.
