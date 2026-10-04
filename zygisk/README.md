# beIN Cast Root Prototype (v7)

An experimental Magisk / KernelSU module for the official beIN CONNECT MENA 10.4.1 app on Android 13, restricted to the inspected ARM64 ART build `a47994c420371ffbd05d16fc9a15ac9f`. It uses a working Zygisk implementation, including ReZygisk, to change a Cast option in the app's process. It does not require an LSPosed module scope.

**Casting validation is pending. This is a test candidate, not a confirmed casting fix.** Version 6 installed the ARM visibility hook, then failed at the removed JIT collection function before any Cast override. Its subsequent startup crash repeated the inert LSPosed crash path. Inspection of the device's actual ART binary confirms the supported `DoCollection(Thread*)` replacement and different class-initialization status values. Version 7 adapts those requirements for this exact binary and checks the build ID, Java debugging state, and mandatory symbol groups before loading its callback or installing native hooks. See [native/ART-PROFILE.md](native/ART-PROFILE.md) for the evidence and limits.

## What it changes

Only the main `ptv.bein.mena` process is selected. A small in-memory DEX callback and standalone LSPlant install a hook on `Application.attach(Context)` to obtain the real app classloader. After the original attach completes, a hook on `CastOptions.Builder.setShowSystemOutputSwitcherOnCastIconClick(boolean)` calls the original setter with `false`. The inspected app's `CastOptionsProvider.getCastOptions(Context)` is deoptimized to keep the setter call from being bypassed by inlining.

The setter is explicitly passed `true` in beIN 10.4.1; the inspected 10.3.7 provider does not set it. Disabling this option is intended to restore the Cast device chooser. It does not establish that video will play on the receiver: that needs a real casting test.

The app APK, installed signature, credentials, receiver ID, reconnection setting, DRM, license requests, and media URLs are not edited. The module has no system overlay or boot-time patch script. It does not replace LSPosed or the root implementation. For other processes, the native module requests unloading before loading the helper or initializing ART hooks.

## Install and test

1. Keep the official, unmodified beIN CONNECT 10.4.1 installed.
2. Exclude beIN from **every LSPosed module's scope** so the earlier crashing framework path is not also active.
3. Install `beIN-Cast-Root-v7-prototype.zip` in the Magisk or KernelSU manager; its module ID is unchanged, so it replaces the earlier prototype. Keep your working ReZygisk/Zygisk implementation enabled. KernelSU alone does not implement the Zygisk API.
4. Reboot and launch beIN. If startup succeeds, try the Cast button and check whether the Chromecast device chooser returns. If it does, connect and test video on the TV. If startup crashes, save the log, then disable the prototype and reboot.

Capture diagnostics in Termux. Enter root first:

```sh
su
```

Then clear the log and stop beIN:

```sh
logcat -c
am force-stop ptv.bein.mena
```

Open beIN. If it stays open, tap Cast until the chooser appears. Return to Termux and save:

```sh
logcat -d -v threadtime > /sdcard/Download/bein-root-v7.txt
```

The `BeINCastRoot` tag reports runtime identity, preflight, initialization, hook installation, and the first overridden setter call. The decisive callback message is `Cast system output switcher option forced false`. A loaded native module or `startup hook ready=1` alone is not evidence that the Cast override ran. A different or absent build ID, 32-bit runtime, Java-debuggable runtime, or missing mandatory symbol causes a skip before callback loading or native hook installation. This preflight does not guarantee backend success or provide rollback for a later partial LSPlant initialization failure.

To undo the experiment, disable or uninstall **beIN Cast Root Prototype** in the root manager and reboot. No app reinstall or data clear is required.

## Build and verification

The GitHub Actions workflow builds ARM32 and ARM64 using JDK 17, Android SDK 34, Build Tools 35.0.0, NDK 27.3.13750724, and CMake 3.22.1, preinstalled on the Ubuntu 22.04 runner. The Java helper uses only existing public APIs and its DEX minimum API is 26; the module's runtime gate remains Android 13/API 33. All source dependencies are pinned; see [THIRD_PARTY.md](THIRD_PARTY.md) and [native/DEPENDENCIES.md](native/DEPENDENCIES.md).

```sh
bash zygisk/build.sh
```

The pinned LSPlant headers are modified only through the checked script in [patches/](patches/README.md), which records exact source hashes and upstream references. The JIT replacement preserves obsolete-method migration and mandatory collection coverage. Local entrypoint-rewrite protection and an early ArtMethod size check address the inspected binary's paths; these are not generic upstream patches. The module checks that profile before entering LSPlant.

Host compatibility checks cover patch application, actual hook-helper backend success/failure handling, profile rejection, and mandatory symbol groups. Resolver checks exercise GNU build-ID notes, compressed mini-ELF lookup, and malformed input handling. The uploaded runtime was checked locally with the production resolver and real ELF load segments; its binary is not included in the repository or ZIP. Callback checks cover argument overriding, original behavior, exception propagation, and continuing app initialization after a failed Cast setup. ZIP validation checks payloads, DEX signature, and ELF architectures. Compilation and host checks do not validate Android ART hooking or Chromecast playback.

This prototype targets API 33 and one inspected ARM64 ART build only; the ARM32 payload remains compiled for build checks but is rejected by the runtime profile. An ART update may cause a skip until the new binary is reviewed. Cast options created inside `attachBaseContext` would precede installation of this hook. The protected app's classloader and interaction with other native modules remain device-test limitations. The earlier startup crash's mechanism remains unproved; these changes do not establish that beIN will accept instrumentation. No request to hide instrumentation or bypass app integrity/DRM checks is implemented.
