# beIN Cast Fix

The latest build is a **KernelSU Next / Magisk Zygisk diagnostic (v9)** for official beIN CONNECT MENA on Android 13 and one inspected ARM64 ART build. Install it through the root manager, with working ReZygisk/Zygisk. See [zygisk/README.md](zygisk/README.md) for installation, the automatic two-stage capture, and rollback.

Matched v8 launches showed that the resident module and read-only preflight passed splash, while adding LSPlant initialization caused a protected Java startup crash before any Java/Cast hook. V9 separates engine preparation without native interception from individual native ART hook groups. Its first two automatic trials are preparation and class visibility. It contains no helper DEX, Java hooking or casting change; a working Cast fix is not yet established.

The older `app/` project retains the modern inert LSPosed diagnostic. Keep beIN outside every LSPosed module's scope during root-module trials. Raw uploaded logs and the device ART library are not published in this repository.
