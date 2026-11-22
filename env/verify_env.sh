#!/usr/bin/env bash
# Non-root pre-flight check. Run before every measurement session and save the
# output alongside the data. Fails (exit 1) if the effective frequency of any
# instruction class deviates from TSC by more than TOL_PCT.
# Usage: env/verify_env.sh [cpu=30] [tol_pct=2]
set -euo pipefail
CPU=${1:-30}; TOL_PCT=${2:-2}
ROOT=$(cd "$(dirname "$0")/.." && pwd); BIN="$ROOT/build/freq_probe"
[ -x "$BIN" ] || make -C "$ROOT" build/freq_probe >/dev/null
c=/sys/devices/system/cpu
echo "cpu=$CPU sibling=$(cat $c/cpu$CPU/topology/thread_siblings_list) numa=$(basename $(ls -d $c/cpu$CPU/node*))"
echo "isolated=$(cat $c/isolated) nohz_full=$(tr -d ' ' < $c/nohz_full)"
echo "governor=$(cat $c/cpu$CPU/cpufreq/scaling_governor) min=$(cat $c/cpu$CPU/cpufreq/scaling_min_freq) max=$(cat $c/cpu$CPU/cpufreq/scaling_max_freq) no_turbo=$(cat $c/intel_pstate/no_turbo)"
echo "cstates: $(for s in $c/cpu$CPU/cpuidle/state*; do printf '%s(dis=%s) ' $(cat $s/name) $(cat $s/disable); done)"
echo "user tasks currently on cpu$CPU: $(ps -eo psr,comm --no-headers | awk -v a=$CPU '$1==a{print $2}' | grep -Ev '^(kworker|ksoftirq|migration|cpuhp|rcu|idle_inject|watchdog)' | tr '\n' ' ')"
TSC_MHZ=$(perf stat -x, -C "$CPU" -e msr/tsc/ sleep 1 2>&1 | awk -F, '/tsc/{printf "%.0f", $1/$4*1e3}')
TSC_MHZ=${TSC_MHZ:-2000}
echo "tsc_mhz~$TSC_MHZ"
rc=0
for m in scalar avx512 amx; do
  read -r ap mp < <(perf stat -x, -e msr/aperf/,msr/mperf/ taskset -c "$CPU" "$BIN" "$m" 2 2>&1 | awk -F, '/aperf/{a=$1}/mperf/{m=$1}END{print a, m}')
  eff=$(awk -v a="$ap" -v m="$mp" -v t="$TSC_MHZ" 'BEGIN{printf "%.0f", t*a/m}')
  dev=$(awk -v e="$eff" -v t="$TSC_MHZ" 'BEGIN{d=(e-t)/t*100; printf "%.1f", d<0?-d:d}')
  ok=$(awk -v d="$dev" -v tol="$TOL_PCT" 'BEGIN{print (d<=tol)?"OK":"FAIL"}')
  printf '%-7s effective=%5s MHz  dev=%5s%%  %s\n' "$m" "$eff" "$dev" "$ok"
  [ "$ok" = OK ] || rc=1
done
[ $rc -eq 0 ] && echo "PASS: frequency pinned to TSC" || echo "WARN: core is NOT running at TSC frequency -> cycle counts are confounded by DVFS (see PLAN.md §Platform)"
exit $rc
