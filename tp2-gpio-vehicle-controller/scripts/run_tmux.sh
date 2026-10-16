#!/usr/bin/env bash
# Opens controller, sensors and panel side by side in a tmux session.
# Usage: scripts/run_tmux.sh [sim]
#   (no argument)  Raspberry Pi build, needs root for GPIO access
#   sim            simulated build (`make SIM=1`), runs on any Linux machine
set -euo pipefail

cd "$(dirname "$0")/.."
command -v tmux >/dev/null || { echo "tmux is required for this helper." >&2; exit 1; }
[ -x build/controller ] || { echo "Build the project first: make (or make SIM=1)" >&2; exit 1; }

SUDO=""
if [ "${1:-}" != "sim" ]; then
    SUDO="sudo"
fi

SESSION=tp2
tmux kill-session -t "$SESSION" 2>/dev/null || true
tmux new-session -d -s "$SESSION" "$SUDO ./build/controller"
sleep 1 # the controller creates the IPC objects the other two attach to
tmux split-window -h -t "$SESSION" "$SUDO ./build/sensors"
tmux split-window -v -t "$SESSION" "$SUDO ./build/car_panel"
tmux attach-session -t "$SESSION"
