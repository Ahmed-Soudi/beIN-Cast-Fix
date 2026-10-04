#!/system/bin/sh

ui_print "beIN Cast Root Prototype — experimental"
ui_print "Requires working Zygisk / ReZygisk; no LSPosed scope needed."

if [ "$BOOTMODE" != true ]; then
    abort "Install from the Magisk or KernelSU manager while Android is running."
fi

if [ "$API" -ne 33 ]; then
    abort "This reviewed prototype targets Android 13 / API 33 only."
fi

case "$ARCH" in
    arm64)
        ;;
    *)
        abort "Version 7 targets the reviewed ARM64 ART build only."
        ;;
esac

# No APK changes, data-directory overlays, framework replacements, or boot scripts.
set_perm_recursive "$MODPATH" 0 0 0755 0644
ui_print "Keep official beIN installed and exclude beIN from every LSPosed module scope."
ui_print "The native module checks the reviewed ART build ID before installing hooks."
ui_print "Reboot after installing. Disable this module and reboot to undo it."
