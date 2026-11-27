#!/usr/bin/env bash
# Phase-2: sample RAPL package + DRAM energy counters (world-readable sysfs)
# at a fixed period, e.g. alongside hold_state for a power time series.
# Usage: bench/power_sampler.sh [pkg=0] [period_ms=100] [secs=10] > out.csv
# CSV: t_ns,pkg_uj,dram_uj  (cumulative; diff in analysis, wraps at max_energy_range_uj)
set -euo pipefail
PKG=${1:-0}; PER=${2:-100}; SECS=${3:-10}
Z=""
for z in /sys/class/powercap/intel-rapl:*; do
  [ -r "$z/name" ] && [ "$(cat "$z/name")" = "package-$PKG" ] && { Z=$z; break; }
done
[ -n "$Z" ] || { echo "no RAPL zone package-$PKG" >&2; exit 1; }
D=""
for z in "$Z"/intel-rapl:*; do
  [ -r "$z/name" ] && [ "$(cat "$z/name")" = dram ] && { D=$z; break; }
done
SLP=$(awk -v p="$PER" 'BEGIN{print p/1000}')
end=$(( $(date +%s%N) + SECS * 1000000000 ))
echo "t_ns,pkg_uj,dram_uj"
while :; do
  t=$(date +%s%N); [ "$t" -ge "$end" ] && break
  echo "$t,$(cat "$Z/energy_uj"),$([ -n "$D" ] && cat "$D/energy_uj" || echo 0)"
  sleep "$SLP"
done
