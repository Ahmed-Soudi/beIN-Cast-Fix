# beIN Cast Root Prototype (v6)

An experimental Magisk / KernelSU module for the official beIN CONNECT MENA 10.4.1 app on Android 13. It uses a working Zygisk implementation, including the existing ReZygisk setup, to change a Cast option in the app's process. It does not require an LSPosed module scope.

**Casting validation is pending. This is a test candidate, not a confirmed casting fix.** The v4 log showed that the engine could not find `GetMethodShorty`. The v5 phone log confirmed that compressed `.gnu_debugdata` lookup fixed that failure, then initialization stopped because `FixupStaticTrampolines` was unavailable. Neither version installed its Cast hook. Version 6 applies a checked compatibility backport based on upstream's handling of the same missing-function error in a Samsung report. It requires a successful class-initialization hook through either Fixup or the existing ARM visibility callbacks, and reports backend failure honestly. The phone still needs to validate initialization and chooser behavior.

## What it changes

Only the main `ptv.bein.mena` process is selected. A small in-memory DEX callback and standalone LSPlant install a hook on `Application.attach(Context)` to obtain the real app classloader. After the original attach completes, a hook on `CastOptions.Builder.setShowSystemOutputSwitcherOnCastIconClick(boolean)` calls the original setter with `false`. The inspected app's `CastOptionsProvider.getCastOptions(Context)` is deoptimized to keep the setter call from being bypassed by inlining.

The setter is explicitly passed `true` in beIN 10.4.1; the inspected 10.3.7 provider does not set it. Disabling this option is intended to restore the Cast device chooser. It does not establish that video will play on the receiver: that needs a real casting test.

The app APK, installed signature, credentials, receiver ID, reconnection setting, DRM, license requests, and media URLs are not edited. The module has no system overlay or boot-time patch script. It does not replace LSPosed or the root implementation. For other processes, the native module requests unloading before loading the helper or initializing ART hooks.

## Install and test

1. Keep the official, unmodified beIN CONNECT 10.4.1 installed.
2. Exclude beIN from **every LSPosed module's scope** so the earlier crashing framework path is not also active.
3. Install `beIN-Cast-Root-v6-prototype.zip` in the Magisk or KernelSU manager; its module ID is unchanged, so it replaces the earlier prototype. Keep your working ReZygisk/Zygisk implementation enabled. KernelSU alone does not implement the Zygisk API.
4. Reboot, launch beIN, and try the Cast button. Check whether the Chromecast device chooser returns. If it does, connect and test video on the TV.

Capture the module diagnostics with:

```sh
adb logcat -c
adb shell am force-stop ptv.bein.mena
```

Open beIN manually and tap Cast until the chooser appears. Then run:

```sh
adb logcat -d -v threadtime > bein-root-v6.txt
```

The `BeINCastRoot` tag reports runtime symbol-table decoding, initialization, hook installation, and the first overridden setter call. The decisive callback message is `Cast system output switcher option forced false`. A loaded native module or `startup hook ready=1` alone is not evidence that the Cast override ran. If the app crashes or casting does not change, retain the log before disabling the module.

For Termux, run `su` by itself first, then `logcat -c` and `am force-stop ptv.bein.mena`. Open beIN and tap Cast until the chooser appears, return to Termux, and run `logcat -d -v threadtime > /sdcard/Download/bein-root-v6.txt`.

To undo the experiment, disable or uninstall **beIN Cast Root Prototype** in the root manager and reboot. No app reinstall or data clear is required.

## Build and verification

The GitHub Actions workflow builds ARM32 and ARM64 using JDK 17, Android SDK 34, Build Tools 35.0.0, NDK 27.3.13750724, and CMake 3.22.1, preinstalled on the Ubuntu 22.04 runner. The Java helper uses only existing public APIs and its DEX minimum API is 26; the module's runtime gate remains Android 13/API 33. All source dependencies are pinned; see [THIRD_PARTY.md](THIRD_PARTY.md) and [native/DEPENDENCIES.md](native/DEPENDENCIES.md).

```sh
bash zygisk/build.sh
```

The pinned LSPlant headers are modified only through the checked script in [patches/](patches/README.md), which records exact source hashes and the upstream reference. The backport preserves mandatory registration and interpreter requirements and refuses initialization if it cannot install class-initialization coverage.

Host compatibility checks cover the patch application and actual hook-helper backend success/failure handling. Resolver checks exercise compressed mini-ELF symbol lookup and malformed input handling. Callback checks cover argument overriding, original behavior, exception propagation, and continuing app initialization after a failed Cast setup. ZIP validation checks required payloads, DEX signature, and ELF architectures. Compilation and host checks do not validate Android ART hooking or Chromecast playback.

This prototype intentionally targets API 33 only. Cast options created inside `attachBaseContext` would precede installation of this hook. ART-symbol availability, the protected app's classloader, and interaction with other native modules also remain device-test limitations. No request to hide instrumentation or bypass app integrity/DRM checks is implemented.
