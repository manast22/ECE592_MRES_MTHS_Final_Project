#!/usr/bin/env bash
# Phase-1 context logger: what the rest of the socket is doing during a sweep.
# Every period: busy fraction of every socket-0 CPU (from /proc/stat) except
# the test CPU and its SMT sibling, the sibling's own busy fraction, and
# package power (RAPL). Run in the background next to amx_probe; join on t_ns
# (CLOCK_REALTIME, same clock as amx_probe's t_ns column).
# Usage: bench/sys_logger.sh <test_cpu> [period_s=1] > sys.csv   (kill to stop)
# CSV: t_ns,other_busy_cpus,sib_busy,pkg_w
set -uo pipefail
CPU=${1:?test cpu}; PER=${2:-1}
SIB=$(cut -d, -f2 /sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list | cut -d- -f2)
[ "$SIB" = "$CPU" ] && SIB=$(cut -d, -f1 /sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list)
PKG=$(cat /sys/devices/system/cpu/cpu$CPU/topology/physical_package_id)
Z=""; for z in /sys/class/powercap/intel-rapl:*; do [ "$(cat $z/name 2>/dev/null)" = "package-$PKG" ] && Z=$z && break; done
# socket CPUs (online)
S=""; for d in /sys/devices/system/cpu/cpu[0-9]*; do
  n=${d##*cpu}; [ -r $d/topology/physical_package_id ] || continue
  [ "$(cat $d/topology/physical_package_id)" = "$PKG" ] && S="$S $n"; done
snap() { awk -v list="$S" 'BEGIN{split(list,a," "); for(i in a) w["cpu"a[i]]=1}
  $1 in w { idle=$5+$6; tot=0; for(i=2;i<=NF;i++) tot+=$i; printf "%s %d %d\n", substr($1,4), tot, idle }' /proc/stat; }
echo "t_ns,other_busy_cpus,sib_busy,pkg_w"
prev=$(snap); e0=$(cat $Z/energy_uj); t0=$(date +%s%N)
while sleep "$PER"; do
  cur=$(snap); e1=$(cat $Z/energy_uj); t1=$(date +%s%N)
  join <(echo "$prev" | sort -k1,1) <(echo "$cur" | sort -k1,1) | awk -v c=$CPU -v s=$SIB -v t=$t1 -v e0=$e0 -v e1=$e1 -v dt=$((t1-t0)) '
    { dt_=$4-$2; di=$5-$3; b=dt_>0 ? 1-di/dt_ : 0
      if ($1==s) sib=b; else if ($1!=c) other+=b }
    END { de=e1-e0; if (de<0) de=0; printf "%d,%.2f,%.2f,%.2f\n", t, other, sib, de/(dt/1e3) }'
  prev=$cur; e0=$e1; t0=$t1
done
