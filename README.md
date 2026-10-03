# beIN Cast Fix — v2-inert diagnostic

Diagnostic LSPosed/Xposed module for `ptv.bein.mena`.

## Purpose

This build deliberately performs **no hook at all**. Its `handleLoadPackage()` only checks the package name and returns without logging, reflection, class lookup, or touching Google Cast.

Test:
1. Install the APK.
2. Enable the module in LSPosed.
3. Scope it only to beIN CONNECT (`ptv.bein.mena`).
4. Force-stop beIN and open it.

Interpretation:
- If beIN still crashes at the splash screen, merely loading this Xposed module into beIN is enough to reproduce the problem.
- If beIN opens normally, something executed by v1 caused the crash and can be reintroduced incrementally.

Targeted LSPosed runtime during development: v2.2.0 build 7854.
