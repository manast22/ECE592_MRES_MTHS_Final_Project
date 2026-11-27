#!/usr/bin/env bash
# ROOT-ONLY safety timer: after MINUTES, run `setup_env.sh --restore` in the
# background (survives logout). If you already restored by hand, there is no
# saved state and it does nothing. Arm it right after `sudo env/setup_env.sh`.
# Usage: sudo env/auto_restore.sh [minutes=30]
# Log (incl. the TRACKER line to paste): env/state/auto_restore.log
set -euo pipefail
MIN=${1:-30}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STATE=$ROOT/env/state/setup_env_saved.txt
LOG=$ROOT/env/state/auto_restore.log
[ "$(id -u)" -eq 0 ] || { echo "must run as root (sudo)" >&2; exit 1; }
[[ $MIN =~ ^[0-9]+$ ]] || { echo "minutes must be an integer" >&2; exit 1; }
[ -e "$STATE" ] || { echo "no saved state: run sudo env/setup_env.sh first" >&2; exit 1; }
mkdir -p "$(dirname "$LOG")"
setsid nohup bash -c "sleep $((MIN * 60)); echo \"== \$(date) auto-restore fired\";
  if [ -e '$STATE' ]; then '$ROOT/env/setup_env.sh' --restore; else echo 'already restored by hand, nothing to do'; fi" \
  >> "$LOG" 2>&1 < /dev/null &
echo "auto-restore armed: turbo restores at $(date -d "+$MIN min" '+%H:%M') (pid $!, log $LOG)"
