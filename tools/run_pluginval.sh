#!/usr/bin/env bash
# run_pluginval.sh -- tests a plugin with pluginval (https://github.com/Tracktion/pluginval)
#
# Usage:  tools/run_pluginval.sh <path/to/YourPlugin.vst3> [runs]
#         (macOS: also works with <YourPlugin.component>)
#
# Runs pluginval at the highest strictness level (10) several times (default 3), because
# some bugs (threading, uninitialised values) only show up now and then. Each run gets a
# temporary home folder, so your real presets and settings are not touched.
# A run fails if pluginval fails or if JUCE assertions occur (Debug builds print them).
#
# pluginval is taken from $PLUGINVAL, from the PATH, or downloaded once into
# ~/.cache/pluginval.
#
# Exit code 0: all runs passed. Linux/macOS; for Windows use run_pluginval.ps1.

set -u

if [ $# -lt 1 ]; then
    echo "Usage: $0 <path/to/YourPlugin.vst3> [runs]"
    exit 2
fi
plugin=$1
runs=${2:-3}
if [ ! -e "$plugin" ]; then
    echo "Plugin not found: $plugin"
    exit 2
fi
plugin=$(cd "$(dirname "$plugin")" && pwd)/$(basename "$plugin") # absolute: HOME changes below

# --- find or download pluginval ---
case "$(uname -s)" in
    Linux)  zipname=pluginval_Linux.zip; binary=pluginval ;;
    Darwin) zipname=pluginval_macOS.zip; binary=pluginval.app/Contents/MacOS/pluginval ;;
    *)      echo "Unsupported system, use run_pluginval.ps1 on Windows"; exit 2 ;;
esac
cache=${XDG_CACHE_HOME:-$HOME/.cache}/pluginval
if [ -n "${PLUGINVAL:-}" ]; then
    pluginval=$PLUGINVAL
elif command -v pluginval > /dev/null; then
    pluginval=$(command -v pluginval)
elif [ -x "$cache/$binary" ]; then
    pluginval=$cache/$binary
else
    echo "Downloading pluginval to $cache ..."
    mkdir -p "$cache"
    curl -sSfL -o "$cache/$zipname" "https://github.com/Tracktion/pluginval/releases/latest/download/$zipname" \
        && unzip -q -o "$cache/$zipname" -d "$cache" && rm "$cache/$zipname" || { echo "Download failed"; exit 2; }
    pluginval=$cache/$binary
fi
echo "pluginval: $("$pluginval" --version 2>/dev/null | head -1)"
echo "plugin:    $plugin"

# --- test runs ---
logdir=$(mktemp -d)
failed=0
for i in $(seq 1 "$runs"); do
    home=$(mktemp -d)
    log=$logdir/run_$i.log
    HOME=$home "$pluginval" --strictness-level 10 --validate "$plugin" > "$log" 2>&1
    rc=$?
    rm -rf "$home"
    assertions=$(grep -c "JUCE Assertion" "$log")
    if [ $rc -eq 0 ] && [ "$assertions" -eq 0 ]; then
        echo "run $i/$runs: SUCCESS"
    else
        echo "run $i/$runs: FAILED (exit code $rc, $assertions JUCE assertions), log: $log"
        grep -E "FAILED|JUCE Assertion|\*\*\*" "$log" | head -5 | sed 's/^/    /'
        failed=$((failed + 1))
    fi
done

if [ $failed -eq 0 ]; then
    echo "All $runs runs passed."
    rm -rf "$logdir"
    exit 0
fi
echo "$failed of $runs runs failed. Logs: $logdir"
exit 1
