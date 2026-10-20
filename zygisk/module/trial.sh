#!/system/bin/sh
# Run manually from root Termux. This is never a boot/service script.
set -eu

MODDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MODEFILE="$MODDIR/diagnostic_mode"
APP=ptv.bein.mena
ACTIVITY=ptv.bein.mena/com.netcosports.andbeinconnect.ui.splash.SplashActivity

if [ "$(id -u)" != 0 ]; then
    echo "Enter su in Termux first, then run this script."
    exit 1
fi
if ! [ -f "$MODDIR/module.prop" ] || ! grep -q '^versionCode=9$' "$MODDIR/module.prop"; then
    echo "This script must run from the installed v9 module directory."
    exit 1
fi
if [ -e "$MODDIR/disable" ] || [ -e "$MODDIR/remove" ]; then
    echo "Enable the v9 module in KernelSU/Magisk and reboot first."
    exit 1
fi
if [ "$#" -eq 0 ]; then set -- prepare visibility; fi
for MODE in "$@"; do
    case "$MODE" in
        loader|prepare|visibility|registration|status|collection|instrumentation|all) ;;
        *) echo "Unknown trial mode: $MODE"; exit 1 ;;
    esac
done

OUTDIR=${BEIN_TRIAL_LOG_DIR:-/sdcard/Download}
mkdir -p "$OUTDIR"
STAMP=$(date +%Y%m%d-%H%M%S)
REPORT="$OUTDIR/bein-root-v9-trials-$STAMP-$$.txt"
WORK=/data/local/tmp/bein-v9-trial-$$.txt
PIDLOG="$WORK.pid"
LOGGER_PID=
cleanup() {
    if [ -n "$LOGGER_PID" ]; then
        kill "$LOGGER_PID" 2>/dev/null || true
        wait "$LOGGER_PID" 2>/dev/null || true
        LOGGER_PID=
    fi
    # A fresh process must see the resident-library control after testing.
    printf 'loader\n' > "$MODEFILE" || true
    am force-stop "$APP" >> "$REPORT" 2>&1 || true
    rm -f "$WORK" "$PIDLOG"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

printf 'beIN v9 native hook trials\n' > "$REPORT"
printf 'Requested modes: %s\n' "$*" >> "$REPORT"
echo "Recording startup only. Leave beIN untouched until the trials finish."
for MODE in "$@"; do
    echo "Testing $MODE (15 seconds after launch)..."
    printf '\n===== TRIAL mode=%s =====\n' "$MODE" >> "$REPORT"
    am force-stop "$APP" >> "$REPORT" 2>&1
    printf '%s\n' "$MODE" > "$MODEFILE"
    logcat -b main -b system -b crash -c
    logcat -b main -b system -b crash -v threadtime 'BeINCastRoot:I' 'LSPlant:I' 'AndroidRuntime:E' \
        'ActivityManager:I' 'ActivityTaskManager:I' 'ptv.bein.mena:I' \
        'libc:F' 'DEBUG:F' '*:S' > "$WORK" 2>&1 &
    LOGGER_PID=$!
    LAUNCH_OK=0
    if am start -W -n "$ACTIVITY" >> "$REPORT" 2>&1; then
        LAUNCH_OK=1
        sleep 15
    else
        echo "LAUNCH FAILED" >> "$REPORT"
    fi
    kill "$LOGGER_PID" 2>/dev/null || true
    wait "$LOGGER_PID" 2>/dev/null || true
    LOGGER_PID=
    cat "$WORK" >> "$REPORT"
    if [ "$LAUNCH_OK" != 1 ]; then
        echo "INVALID: foreground launch failed; stopping trials." | tee -a "$REPORT"
        break
    fi
    MAIN_PID=$(sed -n 's/^[0-9-]* [0-9:.]* *\([0-9][0-9]*\) *[0-9][0-9]* I BeINCastRoot: selected ptv\.bein\.mena (v9,.*/\1/p' "$WORK" | head -n 1)
    if [ -z "$MAIN_PID" ]; then
        echo "INVALID: no v9 process selection captured; stopping trials." | tee -a "$REPORT"
        break
    fi
    sed -n "/^[0-9-]* [0-9:.]* *$MAIN_PID  *[0-9][0-9]* [VDIWEF] /p" "$WORK" > "$PIDLOG"
    if ! grep -Fq "PROBE RESULT mode=$MODE valid=1 " "$PIDLOG"; then
        echo "INVALID: no successful audited stage for PID $MAIN_PID; stopping trials." | tee -a "$REPORT"
        break
    fi
    if grep -Eq 'FATAL EXCEPTION|Fatal signal' "$PIDLOG"; then
        echo "RESULT mode=$MODE pid=$MAIN_PID: crash captured" | tee -a "$REPORT"
        # No interception test is useful until the preparation control passes.
        if [ "$MODE" = prepare ]; then break; fi
    elif kill -0 "$MAIN_PID" 2>/dev/null; then
        echo "RESULT mode=$MODE pid=$MAIN_PID: process alive after capture" | tee -a "$REPORT"
    else
        echo "RESULT mode=$MODE pid=$MAIN_PID: process exited; inspect log" | tee -a "$REPORT"
        if [ "$MODE" = prepare ]; then break; fi
    fi
done
echo "Finished. Restoring loader mode and stopping beIN." | tee -a "$REPORT"
echo "Send this file: $REPORT"
