# beIN Cast Fix

The current experiment is a **Magisk / KernelSU Zygisk module** for official beIN CONNECT MENA 10.4.1 on Android 13. It changes the Cast output-switcher option in memory through a standalone ART hook. See [zygisk/README.md](zygisk/README.md) for build, installation, logs, and rollback.

**It is a prototype; casting has not yet been verified on the device.** Version 6 failed before its Cast override and repeated the earlier startup crash path. Version 7 uses the supported JIT collection replacement and protects entrypoint rewrites in the inspected ARM64 ART binary. It checks the exact build ID and required release-runtime symbols before installing hooks. A new device test is required; the startup crash's mechanism remains unproved.

The `app/` project retains the v3 modern inert LSPosed diagnostic module. Both inert legacy and modern scoped tests crashed, so no working LSPosed Cast fix is claimed. Re-signed APK experiments also failed startup. The new root module requires working Zygisk/ReZygisk and beIN excluded from all LSPosed scopes.
