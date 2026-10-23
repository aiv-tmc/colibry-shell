#!/usr/bin/env bash
# Colibry end-to-end smoke test (stage 8).
#
# Starts the compositor with its startup watchdog as the failsafe, waits for
# the expected log markers, optionally opens a terminal to verify window
# mapping, exercises the IPC socket, and fails on crash markers in the log.
#
# Usage: tests/e2e/smoke.sh [path-to-colibry-binary]
# Run from a TTY (the test takes over the console) or nested inside another
# Wayland compositor.
set -u

BINARY="${1:-./build/colibry}"
LOG="$(mktemp /tmp/colibry-smoke.XXXXXX.log)"

cleanup() {
    kill -TERM "$COLIBRY_PID" 2>/dev/null
    wait "$COLIBRY_PID" 2>/dev/null
    rm -f "$LOG"
}
trap cleanup EXIT

echo "smoke: starting $BINARY"
"$BINARY" 2>"$LOG" &
COLIBRY_PID=$!

# Waits for startup to settle; the compositor's own watchdog bounds hangs.
sleep 5

FAIL=0

# Requires a log marker, reporting it when missing.
require() {
    if ! grep -qF "$1" "$LOG"; then
        echo "smoke: MISSING marker: $1"
        FAIL=1
    else
        echo "smoke: ok: $1"
    fi
}

require "wayland socket:"
require "entering event loop"
require "cursor loaded"

# Crash markers must never appear.
for marker in "segfault" "watchdog" "frame commit failed"; do
    if grep -qF "$marker" "$LOG"; then
        echo "smoke: FORBIDDEN marker present: $marker"
        FAIL=1
    fi
done

# Opens a terminal when one is available and expects a mapped window.
SOCKET_NAME="$(grep -oP 'WAYLAND_DISPLAY=\K\S+' "$LOG" | head -n1)"
if [ -n "$SOCKET_NAME" ] && command -v foot >/dev/null 2>&1; then
    WAYLAND_DISPLAY="$SOCKET_NAME" foot >/dev/null 2>&1 &
    TERMINAL_PID=$!
    sleep 4

    require "window mapped"

    kill -TERM "$TERMINAL_PID" 2>/dev/null
    wait "$TERMINAL_PID" 2>/dev/null
else
    echo "smoke: skipped terminal check (no foot or no socket)"
fi

# Exercises the IPC socket.
if [ -n "$SOCKET_NAME" ]; then
    if ./build/colibryctl version | grep -q "colibry"; then
        echo "smoke: ok: ipc version"
    else
        echo "smoke: ipc version failed"
        FAIL=1
    fi
fi

# Clean shutdown through the IPC.
./build/colibryctl exit >/dev/null 2>&1
sleep 1

if kill -0 "$COLIBRY_PID" 2>/dev/null; then
    echo "smoke: compositor did not exit on 'colibryctl exit'"
    FAIL=1
else
    echo "smoke: ok: clean exit"
fi

if [ "$FAIL" -eq 0 ]; then
    echo "smoke: PASS"
else
    echo "smoke: FAIL (log was kept at $LOG)"
    trap - EXIT
fi

exit "$FAIL"
