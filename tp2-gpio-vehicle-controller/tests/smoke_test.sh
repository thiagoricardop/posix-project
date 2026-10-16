#!/usr/bin/env bash
# End-to-end test of the simulated build (make SIM=1): drives the virtual GPIO
# table with gpio_ctl and checks the actuator outputs of the controller.
# Usage: tests/smoke_test.sh [build-dir]
set -euo pipefail

BIN="${1:-build}"
LOG_DIR="$(mktemp -d)"
CONTROLLER_LOG="$LOG_DIR/controller.log"
SENSORS_LOG="$LOG_DIR/sensors.log"
PIDS=()

ctl() { "$BIN/gpio_ctl" "$@"; }

cleanup() {
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
    ctl reset >/dev/null 2>&1 || true
    rm -rf "$LOG_DIR"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    echo "--- controller log (tail) ---" >&2
    tr '\r' '\n' <"$CONTROLLER_LOG" 2>/dev/null | tail -n 5 >&2 || true
    echo "--- sensors log (tail) ---" >&2
    tr '\r' '\n' <"$SENSORS_LOG" 2>/dev/null | tail -n 5 >&2 || true
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

pin_is() { [ "$(ctl get "$1")" = "$2" ]; }
pwm_is() { [ "$(ctl pwm "$1")" = "$2" ]; }
process_gone() { ! kill -0 "$1" 2>/dev/null; }

# last_value <log> <label>: last "<label>: <number>" printed in the log.
last_value() {
    tr '\r' '\n' <"$1" | sed -n "s/.*$2: \\([0-9.]*\\).*/\\1/p" | tail -n 1
}

in_range() { awk -v v="${1:-0}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v >= lo && v <= hi) }'; }
rpm_in_range() { in_range "$(last_value "$CONTROLLER_LOG" RPM)" 1500 3000; }
speed_above_30() { in_range "$(last_value "$CONTROLLER_LOG" Speed)" 30 300; }

press() { ctl set "$1" 1; sleep 0.3; ctl set "$1" 0; }

# 1. Start the three processes (the controller creates the IPC objects).
ctl reset
"$BIN/controller" >"$CONTROLLER_LOG" 2>&1 &
CONTROLLER=$!
PIDS+=("$CONTROLLER")
wait_for 5 test -e /dev/shm/sem.tp2_car_sem || fail "controller did not create its IPC objects"

"$BIN/sensors" >"$SENSORS_LOG" 2>&1 &
SENSORS=$!
PIDS+=("$SENSORS")
"$BIN/car_panel" >"$LOG_DIR/panel.log" 2>&1 </dev/null &
PANEL=$!
PIDS+=("$PANEL")
sleep 0.5

# 2. Headlight button toggles the low beam lamp.
press cmd-low
wait_for 5 pin_is lamp-low 1 || fail "low beam lamp did not turn on"

# 3. Accelerator pedal: 50 % duty on the 10-step (1 kHz) PWM, direction forward.
ctl set accel 1
wait_for 5 pwm_is motor-pwm 5 || fail "motor PWM did not follow the accelerator pedal"
pin_is motor-dir1 1 || fail "motor direction pins are wrong"
ctl set accel 0
wait_for 5 pwm_is motor-pwm 0 || fail "motor PWM did not return to zero"

# 4. Engine Hall sensor: 38 pulses per second must read about 2280 RPM.
ctl pulse hall-motor 38 5 &
PIDS+=("$!")
wait_for 8 rpm_in_range || fail "engine RPM was not measured"

# 5. Brake pedal: full brake duty, brake light on, motor off.
ctl set brake 1
wait_for 5 pwm_is brake-pwm 10 || fail "brake PWM did not follow the brake pedal"
pin_is lamp-brake 1 || fail "brake light did not turn on"
pwm_is motor-pwm 0 || fail "motor was not cut while braking"
ctl set brake 0
wait_for 5 pin_is lamp-brake 0 || fail "brake light did not turn off"

# 6. Turn signal blinks while enabled and stops when toggled off.
press cmd-left
seen_on=0
seen_off=0
for _ in $(seq 1 30); do
    if [ "$(ctl get lamp-left)" = "1" ]; then seen_on=1; else seen_off=1; fi
    sleep 0.1
done
[ "$seen_on" = 1 ] && [ "$seen_off" = 1 ] || fail "left turn signal is not blinking"
press cmd-left
sleep 0.3
for _ in $(seq 1 15); do
    pin_is lamp-left 0 || fail "left turn signal did not turn off"
    sleep 0.1
done

# 7. Cruise control: engage at ~64 km/h, then throttle is requested once the car slows down.
ctl pulse hall-wheel-a 4.5 6 &
PIDS+=("$!")
wait_for 8 speed_above_30 || fail "wheel speed was not measured"
press cc-resume
wait_for 12 pwm_is motor-pwm 5 || fail "cruise control did not request throttle"
press cc-cancel
wait_for 5 pwm_is motor-pwm 0 || fail "cruise control did not release the throttle"

# 8. Ctrl+C (SIGINT) shuts everything down cleanly and turns the actuators off.
kill -INT "$PANEL" "$SENSORS" "$CONTROLLER"
for pid in "$PANEL" "$SENSORS" "$CONTROLLER"; do
    wait_for 10 process_gone "$pid" || fail "process $pid did not stop"
    wait "$pid" || fail "process $pid exited with an error"
done
pwm_is motor-pwm 0 || fail "motor PWM not cleared on exit"
pin_is lamp-low 0 || fail "lamps not cleared on exit"
[ ! -e /dev/shm/tp2_car_shm ] || fail "shared memory was not removed"
[ ! -e /dev/shm/sem.tp2_car_sem ] || fail "semaphore was not removed"

echo "TP2 smoke test passed."
