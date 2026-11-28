#!/usr/bin/env bash
# Turbo-off slot 3: fill the Phase-1 gaps and redo Phase 2 with the A/B probe.
# Run AFTER `sudo env/setup_env.sh` (this script needs no root). ~24 min (p2 only: ~16 min).
# Usage: scripts/run_phase12.sh [cpu=10] [quick|p2|p2b|p2bquick|p2c|p2cquick]
#   quick = smoke test ~3 min, p2 = Phase 2 only (~16 min),
#   p2b = only the slot-3 leftovers (~7 min), p2bquick = its smoke test (~2 min)
#   p2c = matched warm/gated pairs with in-run latency, for the FoM (~9 min), p2cquick = smoke test
#
# Phase 1 (cpu, default 10):
#   P1a AVX width controls  fma512/fma256/fma128/none, 100 us warm-up
#   P1b deep-state conditions: AMX sweep with the rest of the core/socket
#       quiet, SMT sibling spinning, 4 neighbour cores spinning, 4 neighbour
#       cores running AMX; 2 rounds, shuffled. sys_logger records load.
#   P1c AMX after sleep (core may halt) instead of spin; short + long
#   P1d AMX after TILERELEASE (op=full); short + long
#   P1e long spin sweep again (0.18-10 s) for the ~20k state
#   P1f second core (CPU 12): AMX + AVX-512 short sweeps
# Phase 2 (power_ab on 15 physical cores, ABBA windows vs scalar spin):
#   null test, AMX at each state, AVX saturated/gated, core-count scaling
set -uo pipefail
CPU=${1:-10}; QUICK=${2:-}
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
make -s all || { echo "build failed" >&2; exit 1; }

AT=50; LT=4; LS=6; VT=200; CT=100; WIN=64       # trials / 1-s power windows
[ "$QUICK" = quick ] && { AT=3; LT=1; LS=1; VT=10; CT=5; WIN=8; }
{ [ "$QUICK" = p2bquick ] || [ "$QUICK" = p2cquick ]; } && WIN=8
NBR=12-15; CPU2=12; PCPUS=2-9,11-17               # neighbour cores, 2nd core, power cores
OUT=data/raw/phase12_$(hostname -s)_$(date +%Y%m%d-%H%M)${QUICK:+_$QUICK}
mkdir -p "$OUT"
T="taskset -c $CPU"
log() { echo "== $(date +%T) $*" | tee -a "$OUT/run.log"; }
SIB=$(cut -d, -f2 /sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list)

{ date; uname -r; echo "cpu=$CPU sib=$SIB nbr=$NBR cpu2=$CPU2 power_cpus=$PCPUS quick=${QUICK:-no}"
  env/setup_env.sh --status; md5sum bench/*.c bench/common/*.h bench/*.sh env/*.sh scripts/*.sh; } > "$OUT/meta.txt" 2>&1

log "verify_env (start)"
env/verify_env.sh "$CPU" 2>&1 | tee "$OUT/verify_start.txt"
[ "${PIPESTATUS[0]}" -eq 0 ] || log "WARNING: frequency NOT pinned (turbo on?). Data saved but confounded by DVFS."
$T build/check_amx > "$OUT/check_amx.txt" 2>&1

case "$QUICK" in p2*) PH1=no;; *) PH1=yes;; esac
if [ $PH1 = yes ]; then   # ===== Phase 1 (skipped in p2* modes) =====
bench/sys_logger.sh "$CPU" 1 > "$OUT/sys.csv" & LOGPID=$!
trap 'kill $LOGPID 2>/dev/null' EXIT

# ---------- P1a: AVX width controls ----------
for op in fma512 fma256 fma128 none; do
  log "P1a avx512_probe $op (trials=$VT warm_us=100)"
  $T build/avx512_probe cpu="$CPU" trials=$VT op=$op warm_us=100 > "$OUT/avx_${op}.csv" 2> "$OUT/avx_${op}.txt"
done

# ---------- P1b: deep-state conditions ----------
bg_load() {   # $1 = condition; prints PIDs of background load
  case $1 in
    quiet) ;;
    sib_spin) taskset -c "$SIB" bash -c 'while :; do :; done' > /dev/null & echo $! ;;
    nbr_spin) for c in $(seq ${NBR%-*} ${NBR#*-}); do taskset -c $c bash -c 'while :; do :; done' > /dev/null & echo $!; done ;;
    nbr_amx)  for c in $(seq ${NBR%-*} ${NBR#*-}); do taskset -c $c build/hold_state cpu=$c op=dpbssd delta_ns=0 burst=64 secs=600 > /dev/null & echo $!; done ;;
  esac
}
for r in 1 2; do
  for cond in $(printf "quiet\nsib_spin\nnbr_spin\nnbr_amx\n" | shuf); do
    log "P1b amx_probe dpbssd_st cond=$cond round=$r (trials=$AT)"
    pids=$(bg_load $cond); sleep 1
    $T build/amx_probe cpu="$CPU" trials=$AT op=dpbssd_st > "$OUT/amx_cond_${cond}_r$r.csv" 2> "$OUT/amx_cond_${cond}_r$r.txt"
    # (never a bare `wait`: it would block on sys_logger forever)
    if [ -n "$pids" ]; then kill $pids 2>/dev/null; sleep 0.5; fi
  done
done

# ---------- P1c / P1d: sleep idle, TILERELEASE ----------
for v in "dpbssd_st sleep" "full spin"; do
  set -- $v
  log "P1 amx_probe op=$1 idle=$2 short (trials=$AT)"
  $T build/amx_probe cpu="$CPU" trials=$AT op=$1 idle=$2 > "$OUT/amx_${1}_${2}.csv" 2> "$OUT/amx_${1}_${2}.txt"
  log "P1 amx_probe op=$1 idle=$2 LONG (trials=$LT)"
  $T build/amx_probe cpu="$CPU" trials=$LT op=$1 idle=$2 dmin_ns=1.7e8 dmax_ns=1e10 > "$OUT/amx_${1}_${2}_long.csv" 2> "$OUT/amx_${1}_${2}_long.txt"
done

# ---------- P1e: long spin sweep ----------
log "P1e amx_probe dpbssd_st LONG spin (trials=$LS)"
$T build/amx_probe cpu="$CPU" trials=$LS op=dpbssd_st dmin_ns=1.7e8 dmax_ns=1e10 > "$OUT/amx_dpbssd_st_spin_long.csv" 2> "$OUT/amx_dpbssd_st_spin_long.txt"

# ---------- P1g: fine grids (16/decade) around each threshold ----------
#   300 ns-10 us: 133->672 and 672->7.7k->5.7k   |   30 us-1 ms: 5.7k->9.4k (slot 1)
for g in "200 1e4" "3e4 1e6"; do
  set -- $g
  log "P1g amx_probe dpbssd_st fine $1-$2 ns (ppd=16, trials=$AT)"
  $T build/amx_probe cpu="$CPU" trials=$AT op=dpbssd_st ppd=16 dmin_ns=$1 dmax_ns=$2 > "$OUT/amx_fine_$1_$2.csv" 2> "$OUT/amx_fine_$1_$2.txt"
done

# ---------- P1f: second core ----------
log "P1f CPU $CPU2: amx dpbssd_st (trials=$AT), avx fma512/fma256 (trials=$CT)"
taskset -c $CPU2 build/amx_probe cpu=$CPU2 trials=$AT op=dpbssd_st > "$OUT/cpu${CPU2}_amx_dpbssd_st.csv" 2> "$OUT/cpu${CPU2}_amx_dpbssd_st.txt"
for op in fma512 fma256; do
  taskset -c $CPU2 build/avx512_probe cpu=$CPU2 trials=$CT op=$op warm_us=100 > "$OUT/cpu${CPU2}_avx_${op}.csv" 2> "$OUT/cpu${CPU2}_avx_${op}.txt"
done
kill $LOGPID 2>/dev/null; trap - EXIT
fi   # ===== end Phase 1 =====

# ---------- Phase 2: power_ab ----------
P="$OUT/power_ab.csv"; S="$OUT/power_ab.txt"; : > "$S"
echo "label,n_cores,a,b,win,state,dt_s,pkg_w,dram_w,t_ns" > "$P"
pab() {  # label cpus a b
  log "P2 power_ab $1: A=$3 B=$4 cpus=$2 (windows=$WIN x 1 s)"
  build/power_ab label="$1" cpus="$2" a="$3" b="$4" win_ms=1000 windows=$WIN main=$CPU lat_out="$OUT/power_lat.csv" >> "$P" 2>> "$S"
  grep -v "^  lat" "$S" | tail -1
}
pab null        $PCPUS none:0:1          none:0:1
if [ "$QUICK" = p2c ] || [ "$QUICK" = p2cquick ]; then
  # slot 5: matched warm-vs-gated pairs for the FoM (compute_fom.py), each op timed in-run (~9 min)
  pab amx_300ns            $PCPUS dpbssd:300:1    none:300:1
  pab amx_20us             $PCPUS dpbssd:20000:1  none:20000:1
  pab amx_20us_vs_300ns    $PCPUS dpbssd:20000:1  dpbssd:300:1
  pab avx512_300ns         $PCPUS fma512:300:1    none:300:1
  pab avx512_20us_vs_300ns $PCPUS fma512:20000:1  fma512:300:1
  pab avx256_20us          $PCPUS fma256:20000:1  none:20000:1
  pab avx256_20us_vs_300ns $PCPUS fma256:20000:1  fma256:300:1
elif [ "$QUICK" = p2b ] || [ "$QUICK" = p2bquick ]; then
  # slot 4: what the 17:34 rerun lost to the stale timer (~7 min)
  pab avx256_sat  $PCPUS fma256:0:8        none:0:1
  pab avx512_20us $PCPUS fma512:20000:1    none:20000:1
  for n in 1 4 8; do
    cs=$(echo $PCPUS | tr , '\n' | while IFS=- read a b; do seq $a ${b:-$a}; done | head -$n | paste -sd,)
    pab amx_sat_n$n "$cs" dpbssd:0:64 none:0:1
  done
else
pab amx_sat     $PCPUS dpbssd:0:64       none:0:1
pab amx_300ns   $PCPUS dpbssd:300:1      none:300:1
pab amx_1us     $PCPUS dpbssd:1000:1     none:1000:1
pab amx_20us    $PCPUS dpbssd:20000:1    none:20000:1
pab amx_1ms     $PCPUS dpbssd:1000000:1  none:1000000:1
pab amx_20us_vs_300ns $PCPUS dpbssd:20000:1 dpbssd:300:1
pab avx512_sat  $PCPUS fma512:0:8        none:0:1
pab avx256_sat  $PCPUS fma256:0:8        none:0:1
pab avx512_20us $PCPUS fma512:20000:1    none:20000:1
for n in 1 4 8; do
  cs=$(echo $PCPUS | tr , '\n' | while IFS=- read a b; do seq $a ${b:-$a}; done | head -$n | paste -sd,)
  pab amx_sat_n$n "$cs" dpbssd:0:64 none:0:1
done
fi

log "verify_env (end)"
env/verify_env.sh "$CPU" 2>&1 | tee "$OUT/verify_end.txt"
log "done -> $OUT"
