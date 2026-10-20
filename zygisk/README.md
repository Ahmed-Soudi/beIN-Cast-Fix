# beIN Cast Root v9: native hook trials

A Magisk / KernelSU Zygisk diagnostic for the official beIN CONNECT MENA app, restricted to Android 13/API33 and ARM64 ART build `a47994c420371ffbd05d16fc9a15ac9f`. Install this ZIP in the KernelSU Next or Magisk manager. Working ReZygisk/Zygisk is required; no LSPosed module scope is used.

**This diagnoses startup; it does not change casting.** The matched v8 controls established that the resident module and read-only preflight passed splash, while adding LSPlant initialization crashed in `getMediaURL.onExtraCallback`. The engine recheck used the same root launch as the successful loader control. It reached readiness and failed 2.703 seconds later with `ArrayIndexOutOfBoundsException`, before any callback DEX or Java/Cast hook. No specific native hook defect or defensive reaction has been proved.

## What v9 isolates

The same pinned LSPlant/Dobby engine and checked compatibility patches are retained. The module permits only six exact native entrypoints after the same runtime profile, release-state and symbol preflight. Addresses must be nonnull and distinct. Every expected hook request must occur exactly once; unexpected, missing, duplicate or failed requests invalidate the trial.

In preparation mode the backend returns each unchanged original target as its backup and installs no interception. This retains LSPlant JNI/reflection, layout and runtime preparation, while omitting Dobby patches and replacement dispatch. Other modes install only their selected native group and bypass the remaining requests. **The resulting partial engine is exclusively diagnostic. There is no Java hooking, deoptimization, helper DEX loading or Cast lookup path in this module.**

| Mode | Real native interceptions | Bypassed requests |
| --- | ---: | ---: |
| `loader` | 0 (no LSPlant initialization) | 0 |
| `prepare` (packaged default) | 0 | 6 |
| `visibility` | 1: MarkVisiblyInitialized | 5 |
| `registration` | 2: RegisterNative and UnregisterNative | 4 |
| `status` | 1: Class.SetStatus | 5 |
| `collection` | 1: JitCodeCache.DoCollection | 5 |
| `instrumentation` | 1: UpdateMethodsCodeImpl | 5 |
| `all` | 6: same-build full native reference | 0 |

The authoritative line is `BeINCastRoot: PROBE RESULT mode=... valid=1 ...` with the expected counts. LSPlant's internal messages saying a hook was installed also appear for diagnostic bypasses; they do not establish interception. A readiness line is not evidence of successful app startup.

Preparation passing while an individual native group fails associates the crash with that installation condition. It does not distinguish callback correctness, relocation, timing or detection. A passing group might not exercise every callback path. If singles pass and `all` fails, interactions remain possible. Native hook maps remain empty throughout every mode. No changes hide instrumentation or modify integrity/DRM checks.

## Install and first two trials

1. Keep official, unmodified beIN installed. The inspected app is 10.4.1. Exclude beIN from every LSPosed module's scope.
2. Install `beIN-Cast-Root-v9-native-trials.zip` in KernelSU Next/Magisk. It replaces v8 using the same module ID, `bein_cast_root`. Enable the module and your working Zygisk provider, then reboot.
3. Open Termux and enter root separately:

```sh
su
```

Run:

```sh
sh /data/adb/modules/bein_cast_root/trial.sh
```

Leave the app untouched while the script runs. It uses the same root component launch for both stages: first `prepare`, then `visibility`, capturing 15 seconds after each launch. It stops before visibility if preparation crashes or its audit is invalid. Logging starts before launch, explicitly captures main/system/crash buffers, retains the original app PID even if a background service restarts, and writes one small filtered report under `/sdcard/Download/bein-root-v9-trials-*.txt`. Upload that report. A 15-second survival is only a startup observation, not playback validation.

On completion or a normal interruption, the script force-stops beIN and restores `loader` mode for the next fresh process. A killed shell or device restart may prevent shell cleanup; disable the module and reboot to remove the experiment. No data clear or APK reinstall is needed.

## Later trials with the same ZIP

After reviewing the first report, other individual modes can be selected without rebuilding or reinstalling. For example:

```sh
sh /data/adb/modules/bein_cast_root/trial.sh prepare registration
```

Mode files are exact tokens, optionally followed by LF/CRLF, with at most 16 bytes total. Missing or malformed configuration skips setup. The old `engine` and `cast` tokens are rejected. Each change requires a fresh app process; the capture script handles this. Installed hooks remain resident until that process exits, including after a failed initialization. Bypassed targets are never sent to DobbyDestroy.

## Build and verification

```sh
bash zygisk/build.sh
```

The workflow uses JDK17 for retained historical callback checks, NDK27.3.13750724 and CMake3.22.1. Native payloads are compiled for ARM64 and ARM32; runtime setup accepts the reviewed ARM64 profile only. No callback DEX or APK is packaged. All dependencies and five checked engine compatibility patches remain pinned; see [THIRD_PARTY.md](THIRD_PARTY.md), [native/DEPENDENCIES.md](native/DEPENDENCIES.md), and [native/ART-PROFILE.md](native/ART-PROFILE.md).

Host checks cover exact mode parsing, no-backend preparation, exact group membership/counts, registration pairing, invalid/missing/duplicate requests, bad backups, and unhook ownership. Existing checked-patch, callback, profile and ELF resolver suites also run. Packaging requires the exact preparation default and checks native architectures/payload hashes. Compilation and host tests do not establish Android ART transparency or Chromecast playback.
