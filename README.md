# beIN Cast Fix

Experimental LSPosed module for investigating/restoring the casting behavior that changed between beIN CONNECT MENA 10.3.7 and 10.4.

## Target setup

- Samsung Galaxy M127F / Android 13
- KernelSU Next + ReZygisk
- LSPosed
- beIN CONNECT MENA 10.4

## Current status

Project scaffold only. The hook is intentionally inert except for an Xposed log entry. The actual compatibility hook will be based on analysis of the 10.3.7 and 10.4 app builds rather than broad system hooks.

## Build

GitHub Actions builds a debug APK on push, pull request, or manual dispatch.

## Safety

Scope the module to the beIN CONNECT MENA app in LSPosed. Do not enable it system-wide.
