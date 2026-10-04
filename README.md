# beIN Cast Fix

The current experiment is a **Magisk / KernelSU Zygisk module** for official beIN CONNECT MENA 10.4.1 on Android 13. It changes the Cast output-switcher option in memory through a standalone ART hook. See [zygisk/README.md](zygisk/README.md) for build, installation, logs, and rollback.

**It is a prototype; casting has not yet been verified on the device.** The v4 log confirmed that native module loading worked but its ART engine failed before installing any Cast hook. Version 5 adds compressed runtime-symbol lookup to address that initialization failure; a new device test is required.

The `app/` project retains the v3 modern inert LSPosed diagnostic module. Both inert legacy and modern scoped tests crashed, so no working LSPosed Cast fix is claimed. Re-signed APK experiments also failed startup. The new root module requires working Zygisk/ReZygisk and beIN excluded from all LSPosed scopes.
