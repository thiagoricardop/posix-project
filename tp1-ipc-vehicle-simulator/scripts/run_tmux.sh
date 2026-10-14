#!/usr/bin/env bash
# Opens controller, sensors and car panel side by side in a tmux session.
# Usage: scripts/run_tmux.sh   (run `make` first)
set -euo pipefail

cd "$(dirname "$0")/.."
command -v tmux >/dev/null || { echo "tmux is required for this helper." >&2; exit 1; }
[ -x build/controller ] || { echo "Build the project first: make" >&2; exit 1; }

SESSION=tp1
tmux kill-session -t "$SESSION" 2>/dev/null || true
tmux new-session -d -s "$SESSION" "./build/controller"
sleep 1 # the controller creates the IPC objects the other two attach to
tmux split-window -h -t "$SESSION" "./build/sensors"
tmux split-window -v -t "$SESSION" "./build/car_panel"
tmux attach-session -t "$SESSION"
