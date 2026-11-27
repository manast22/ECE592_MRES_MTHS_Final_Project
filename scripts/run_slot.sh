#!/usr/bin/env bash
# One reserved turbo-off slot on artemisia: runs every Phase 1-2 probe on one
# CPU and saves all output under data/raw/slot_<host>_<stamp>/.
# Run AFTER `sudo env/setup_env.sh` (this script needs no root). ~12 min.
# Usage: scripts/run_slot.sh [cpu=10] [quick]     (quick = tiny smoke test, ~1.5 min)
set -uo pipefail
CPU=${1:-10}; QUICK=${2:-}
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
make -s all || { echo "build failed" >&2; exit 1; }

# TR: avx512 trials | AT: amx short-sweep trials | LT/LN: amx long-sweep trials (op / none)
# HS: hold_state secs per run | REPS: hold_state repeats (order shuffled each rep)
TR=200; AT=100; LT=6; LN=4; HS=10; REPS=4; WARM=100
[ "$QUICK" = quick ] && { TR=10; AT=5; LT=1; LN=1; HS=1; REPS=1; }
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

# Halt check, twice (slot 1 saw halts even in the `none` control: is it repeatable?)
for r in 1 2; do
  for op in fma512 fma256 amx none; do
    log "license_timeline $op (run $r)"
    $T build/license_timeline "$CPU" 50 $op 2000 > "$OUT/license_${op}_r$r.csv" 2> "$OUT/license_${op}_r$r.txt"
    tail -1 "$OUT/license_${op}_r$r.txt"
  done
done

# AVX-512 with a sustained warm-up before each idle gap
for op in fma512 fma256 none; do
  log "avx512_probe $op (trials=$TR warm_us=$WARM)"
  $T build/avx512_probe cpu="$CPU" trials=$TR op=$op warm_us=$WARM > "$OUT/avx512_${op}_warm$WARM.csv" 2> "$OUT/avx512_${op}_warm$WARM.txt"
done
cat "$OUT/avx512_fma512_warm$WARM.txt"

# AMX short sweep (0 .. 100 ms), then long sweep (178 ms .. 10 s) for the ~20k state
for op in dpbssd_st dpbssd tileload none; do
  log "amx_probe $op (trials=$AT, 0-100 ms)"
  $T build/amx_probe cpu="$CPU" trials=$AT op=$op > "$OUT/amx_$op.csv" 2> "$OUT/amx_$op.txt"
done
cat "$OUT/amx_dpbssd_st.txt"
for op in dpbssd_st none; do
  n=$LT; [ $op = none ] && n=$LN
  log "amx_probe $op LONG (trials=$n, 178 ms-10 s)"
  $T build/amx_probe cpu="$CPU" trials=$n op=$op dmin_ns=1.7e8 dmax_ns=1e10 > "$OUT/amx_${op}_long.csv" 2> "$OUT/amx_${op}_long.txt"
done
cat "$OUT/amx_dpbssd_st_long.txt"

# Power: one run per AMX state + saturated references + baselines, shuffled per rep.
#   op delta_ns burst
RUNS="none 0 1
none 1000000 1
dpbssd 0 64
fma512 0 8
dpbssd 200 1
dpbssd 1000 1
dpbssd 20000 1
dpbssd 1000000 1"
log "hold_state (secs=$HS reps=$REPS, $(echo "$RUNS" | wc -l) runs/rep)"
H="$OUT/hold_state.csv"; echo "unit,delta_ns,burst,secs,ops,ops_per_s,pkg_w,dram_w,core_mhz,cpu,rep" > "$H"
for r in $(seq "$REPS"); do
  echo "$RUNS" | shuf | while read -r op d b; do
    $T build/hold_state cpu="$CPU" op=$op delta_ns=$d burst=$b secs=$HS | sed "s/\$/,$r/" >> "$H"
  done
done
column -t -s, "$H"

log "verify_env (end)"
env/verify_env.sh "$CPU" 2>&1 | tee "$OUT/verify_end.txt"
log "done -> $OUT"
