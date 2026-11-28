#!/usr/bin/env bash
# ROOT-ONLY safety timer: after MINUTES, run `setup_env.sh --restore` in the
# background (survives logout). If you already restored by hand, there is no
# saved state and it does nothing. Arm it right after `sudo env/setup_env.sh`.
# Each timer is tied to the saved-state file that existed when it was armed
# (inode + mtime + md5). A stale timer from an earlier setup (slot 3: fired
# mid-run after a restore + new setup) sees a different file and does nothing.
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
tok() { echo "$(stat -c '%i:%Y' "$STATE" 2>/dev/null) $(md5sum < "$STATE" 2>/dev/null | cut -c1-32)"; }
TOK=$(tok)
setsid nohup bash -c "sleep $((MIN * 60)); echo \"== \$(date) auto-restore fired (token $TOK)\";
  if [ ! -e '$STATE' ]; then echo 'already restored by hand, nothing to do';
  elif [ \"\$(stat -c '%i:%Y' '$STATE') \$(md5sum < '$STATE' | cut -c1-32)\" != '$TOK' ]; then echo 'stale timer: saved state belongs to a newer setup, nothing to do';
  else '$ROOT/env/setup_env.sh' --restore; fi" \
  >> "$LOG" 2>&1 < /dev/null &
echo "auto-restore armed: turbo restores at $(date -d "+$MIN min" '+%H:%M') (pid $!, log $LOG)"
