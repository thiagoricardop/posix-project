#!/usr/bin/env bash
# End-to-end test: controller + sensors + panel talking over POSIX IPC.
# Usage: tests/smoke_test.sh [build-dir]
set -euo pipefail

BIN="${1:-build}"
LOG_DIR="$(mktemp -d)"
CONTROLLER_LOG="$LOG_DIR/controller.log"
PIDS=()

cleanup() {
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
    rm -rf "$LOG_DIR"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    echo "--- controller log (tail) ---" >&2
    tail -n 25 "$CONTROLLER_LOG" 2>/dev/null | tr -d '\033' >&2 || true
    exit 1
}

# wait_for <seconds> <command...>: retries the command every 100 ms.
wait_for() {
    local tries=$(($1 * 10))
    shift
    for _ in $(seq 1 "$tries"); do
        if "$@"; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

log_has() { grep -aq -- "$1" "$CONTROLLER_LOG"; }

speed_positive() {
    local speed
    speed="$(grep -a 'Speed:' "$CONTROLLER_LOG" | tail -n 1 | awk '{print $2}')"
    awk -v s="${speed:-0}" 'BEGIN { exit !(s > 0) }'
}

process_gone() { ! kill -0 "$1" 2>/dev/null; }

# 1. The controller creates the IPC objects (the semaphore is created last).
"$BIN/controller" >"$CONTROLLER_LOG" 2>&1 &
CONTROLLER=$!
PIDS+=("$CONTROLLER")
wait_for 5 test -e /dev/shm/sem.tp1_sensors_sem || fail "controller did not create its IPC objects"

# 2. Sensors attach to the shared memory.
"$BIN/sensors" >"$LOG_DIR/sensors.log" 2>&1 &
SENSORS=$!
PIDS+=("$SENSORS")

# 3. Drive the panel: low beam, then 50 % accelerator.
printf '3\n1\n50\n' | "$BIN/car_panel" >"$LOG_DIR/panel.log" 2>&1 || fail "car_panel exited with an error"
wait_for 5 log_has "Low beam: ON" || fail "low beam command did not reach the controller"
wait_for 15 speed_positive || fail "vehicle did not accelerate"

# 4. SIGUSR1 pauses and resumes the test.
kill -USR1 "$CONTROLLER"
wait_for 5 log_has "State: PAUSED" || fail "SIGUSR1 did not pause the test"
kill -USR1 "$CONTROLLER"

# 5. The panel's exit key ends the test: report printed, IPC objects removed.
printf '0\n' | "$BIN/car_panel" >>"$LOG_DIR/panel.log" 2>&1 || fail "car_panel exit failed"
wait_for 10 process_gone "$CONTROLLER" || fail "controller did not stop"
wait "$CONTROLLER" || fail "controller exited with an error"
log_has "=== Test report ===" || fail "controller did not print the final report"
[ ! -e /dev/shm/tp1_sensors_shm ] || fail "shared memory was not removed"
[ ! -e /dev/shm/sem.tp1_sensors_sem ] || fail "semaphore was not removed"

# 6. Sensors notice the shutdown and exit on their own.
wait_for 10 process_gone "$SENSORS" || fail "sensors did not stop after the controller"
wait "$SENSORS" || fail "sensors exited with an error"

echo "TP1 smoke test passed."
