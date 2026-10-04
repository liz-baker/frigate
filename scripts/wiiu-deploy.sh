#!/usr/bin/env bash
# Send the built .wuhb to a Wii U with wiiload, then stream its UDP debug log
# (port 4405) until interrupted.
#
#   WIIU_HOST=192.168.1.50 scripts/wiiu-deploy.sh [--no-log]
#
# The console must be sitting in the Homebrew Launcher / Aroma with wiiload
# active. The UDP log is only emitted by Debug builds.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
wuhb="${BUILD_DIR:-$root/build-wiiu}/frigate.wuhb"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
tools="$DEVKITPRO/tools/bin"

do_log=1
for arg in "$@"; do
    case "$arg" in
        --no-log) do_log=0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

: "${WIIU_HOST:?set WIIU_HOST to the console IP or hostname}"
export WIILOAD="tcp:$WIIU_HOST"
[[ -f "$wuhb" ]] || { echo "missing $wuhb - run 'just wiiu_build' first" >&2; exit 1; }

if [[ $do_log -eq 1 ]]; then
    "$tools/udplogserver" 4405 &
    log_pid=$!
    trap 'kill "$log_pid" 2>/dev/null || true' EXIT
fi

"$tools/wiiload" "$wuhb"

if [[ $do_log -eq 1 ]]; then
    echo "wiiload done; streaming UDP log (Ctrl-C to stop)" >&2
    wait "$log_pid"
fi
