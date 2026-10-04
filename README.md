# beIN Cast Fix

The current build is a **Magisk / KernelSU Zygisk startup diagnostic (v8)** for official beIN CONNECT MENA 10.4.1 on Android 13 and one inspected ARM64 ART build. See [zygisk/README.md](zygisk/README.md) for installation, the two startup controls, logs, and rollback.

V7 initialized the engine and installed Java hooks, but beIN crashed in the same protected startup stack as the earlier inert scoped tests, without a logged successful Cast setter override. V8 compares the resident native library with read-only preflight against the same preflight plus unchanged engine initialization. Both diagnostic stages omit callback DEX loading, Java hooks, and Cast lookup. It defaults to engine-only; casting remains unchanged. The crash mechanism and Chromecast playback remain unverified.

The `app/` project retains the v3 modern inert LSPosed diagnostic. Both legacy and modern scoped tests crashed. Re-signed APK experiments also failed startup. The root diagnostic requires working Zygisk/ReZygisk and beIN excluded from all LSPosed scopes.
