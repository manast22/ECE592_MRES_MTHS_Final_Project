#!/usr/bin/env bash
# One reserved turbo-off slot on artemisia: runs every Phase 1-2 probe on one
# CPU and saves all output under data/raw/slot_<host>_<stamp>/.
# Run AFTER `sudo env/setup_env.sh` (this script needs no root). ~6 min.
# Usage: scripts/run_slot.sh [cpu=10] [quick]     (quick = tiny smoke test, ~1 min)
set -uo pipefail
CPU=${1:-10}; QUICK=${2:-}
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
make -s all || { echo "build failed" >&2; exit 1; }

TR=200; AT=100; HS=5; REPS=2
[ "$QUICK" = quick ] && { TR=10; AT=5; HS=1; REPS=1; }
OUT=data/raw/slot_$(hostname -s)_$(date +%Y%m%d-%H%M)${QUICK:+_quick}
mkdir -p "$OUT"
T="taskset -c $CPU"
log() { echo "== $(date +%T) $*" | tee -a "$OUT/run.log"; }

{ date; uname -r; echo "cpu=$CPU quick=${QUICK:-no}"; env/setup_env.sh --status
  md5sum bench/*.c bench/common/*.h bench/*.sh env/*.sh scripts/*.sh; } > "$OUT/meta.txt" 2>&1

log "verify_env (start)"
env/verify_env.sh "$CPU" 2>&1 | tee "$OUT/verify_start.txt"
[ "${PIPESTATUS[0]}" -eq 0 ] || log "WARNING: frequency NOT pinned (turbo on?). Data saved but confounded by DVFS."

log "check_amx";  $T build/check_amx > "$OUT/check_amx.txt" 2>&1; cat "$OUT/check_amx.txt"
log "timer_test"; $T build/timer_test "$CPU" > "$OUT/timer_test.txt" 2>&1; tail -3 "$OUT/timer_test.txt"

for op in fma512 fma256 amx none; do
  log "license_timeline $op"
  $T build/license_timeline "$CPU" 50 $op 2000 > "$OUT/license_$op.csv" 2> "$OUT/license_$op.txt"; tail -1 "$OUT/license_$op.txt"
done

for op in fma512 fma256 none; do
  log "avx512_probe $op (trials=$TR)"
  $T build/avx512_probe cpu="$CPU" trials=$TR op=$op > "$OUT/avx512_$op.csv" 2> "$OUT/avx512_$op.txt"
done

for op in dpbssd dpbssd_st tileload none; do
  log "amx_probe $op (trials=$AT)"
  $T build/amx_probe cpu="$CPU" trials=$AT op=$op > "$OUT/amx_$op.csv" 2> "$OUT/amx_$op.txt"
done
cat "$OUT/amx_dpbssd.txt"

log "hold_state (secs=$HS reps=$REPS)"
H="$OUT/hold_state.csv"; echo "unit,delta_ns,secs,ops,ops_per_s,pkg_w,dram_w,core_mhz,cpu" > "$H"
for r in $(seq "$REPS"); do
  for d in 0 1000 10000 100000 1000000 10000000; do
    for op in none fma512 dpbssd; do
      $T build/hold_state cpu="$CPU" op=$op delta_ns=$d secs=$HS >> "$H"
    done
  done
done
column -t -s, "$H"

log "verify_env (end)"
env/verify_env.sh "$CPU" 2>&1 | tee "$OUT/verify_end.txt"
log "done -> $OUT"
