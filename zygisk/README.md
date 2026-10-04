# beIN Cast Root v8 startup diagnostic

A Magisk / KernelSU Zygisk diagnostic for official beIN CONNECT MENA 10.4.1 on Android 13. It accepts only the inspected ARM64 ART build `a47994c420371ffbd05d16fc9a15ac9f` and a release Java runtime. Working Zygisk/ReZygisk is required; KernelSU alone does not supply the Zygisk API.

**Version 8 diagnoses the startup crash. Its default mode does not change casting.** Version 7 completed engine initialization, installed the attach and Cast hooks, then crashed without a logged successful Cast setter override. Its 725,122,424-byte Java allocation and protected startup stack repeat the earlier inert scoped tests. The Cast override was not observed; the crash mechanism remains unproved. See [native/ART-PROFILE.md](native/ART-PROFILE.md) for the runtime evidence.

## Controlled stages

The packaged `diagnostic_mode` file defaults to `engine`. It is read before specialization, once per fresh main app process. Missing or malformed configuration skips setup. Only the exact main process `ptv.bein.mena` is selected; other processes unload the library.

| Mode | Work performed after the same runtime preflight | Callback DEX / Java hooks / Cast lookup |
| --- | --- | --- |
| `loader` | Retain the native library; read-only ART identity, release-state and symbol checks | None |
| `engine` (default) | The same resident library and checks, followed by the unchanged v7 LSPlant engine initialization | None |
| `cast` | The previous v7 callback and hook path, retained for a future explicit test | Present |

The loader/engine comparison isolates engine initialization from callback loading, Java method deoptimization, Application.attach interception, and provider/Builder initialization. LSPlant initialization installs native ART hooks and changes runtime state; engine mode is not inert. A successful launch in both stages would leave the later Java hook stages to investigate. A failure in both would point to their shared work or a separate environmental cause, requiring log comparison. No result establishes Chromecast playback.

## Install and capture engine startup

1. Keep official, unmodified beIN CONNECT 10.4.1 installed and exclude beIN from every LSPosed module scope.
2. Install `beIN-Cast-Root-v8-diagnostic.zip` through the root manager. The unchanged module ID `bein_cast_root` replaces v7. Keep working ReZygisk/Zygisk enabled.
3. Reboot. Test app startup only; the Cast chooser is expected to remain unchanged in these diagnostic modes.

In Termux, enter root separately:

```sh
su
```

Then prepare the default engine test:

```sh
am force-stop ptv.bein.mena
logcat -c
```

Open beIN, wait for startup or the crash, return to Termux, and save:

```sh
logcat -d -v threadtime > /sdcard/Download/bein-root-v8-engine.txt
```

## Capture the resident-library control

In the same root Termux session, select loader mode and prepare a fresh process:

```sh
printf 'loader\n' > /data/adb/modules/bein_cast_root/diagnostic_mode
am force-stop ptv.bein.mena
logcat -c
```

Open beIN again, wait for startup or the crash, return to Termux, and save:

```sh
logcat -d -v threadtime > /sdcard/Download/bein-root-v8-loader.txt
```

Upload both logs and report whether each launch stayed open or crashed. Changing mode needs a fresh app process, not another reboot; native hooks remain in an already-running process until it exits. To restore the default, write `engine` with the same command and restart the app.

`BeINCastRoot` must report the selected mode and `diagnostic stage ready=1, mode=engine` or `mode=loader`. A skipped or failed stage is not a valid comparison. Loader must not log engine initialization; neither stage should log callback DEX loading, Java hook installation, or `Cast system output switcher option forced false`.

Disable or uninstall **beIN Cast Root Prototype** and reboot after testing. No app reinstall or data clear is required. Raw device logs and the uploaded ART library are not included in the source or package.

## Retained Cast experiment

In explicit `cast` mode only, the previous callback hooks Application.attach after the original method, obtains the app classloader, deoptimizes CastOptionsProvider.getCastOptions, and intercepts CastOptions.Builder.setShowSystemOutputSwitcherOnCastIconClick to call the original with `false`. The option is explicitly `true` in inspected 10.4.1 and unset in 10.3.7. The intended effect is the Chromecast device chooser; TV playback remains unverified. A readiness log alone does not show the setter ran.

The app APK, installed signature, credentials, receiver ID, DRM, license requests, and media URLs are not edited. No framework replacement or boot patch is installed. No integrity or DRM bypass is implemented.

## Build and verification

```sh
bash zygisk/build.sh
```

GitHub Actions uses JDK 17, Android SDK 34, Build Tools 35.0.0, NDK 27.3.13750724, and CMake 3.22.1 on Ubuntu 22.04, with pinned dependencies listed in [THIRD_PARTY.md](THIRD_PARTY.md) and [native/DEPENDENCIES.md](native/DEPENDENCIES.md). The checked v7 engine patches are unchanged; see [patches/](patches/README.md).

Host checks cover strict mode parsing, checked patch application and backend behavior, runtime identity and mandatory symbol rejection, compressed ELF lookup and malformed inputs, and retained Java callback semantics. Packaging requires the exact engine-only default and validates DEX/ELF payloads. ARM32 is compiled for build checks but rejected by the runtime profile. These checks do not establish Android runtime transparency, protected-app startup, or Chromecast playback. Later ART updates require a reviewed profile.
