#!/usr/bin/env bash
# ROOT-ONLY measurement lockdown for artemisia (shared machine).
#  * Touches ONLY the given CPUs (default: socket 0 online CPUs, as agreed with
#    Prof. Mirbagher Ajorpaz; socket 1 is used by other students' experiments).
#  * Never writes the global intel_pstate/no_turbo (that would hit both sockets).
#  * Saves the exact original per-CPU state and restores THAT (not hardcoded values).
#  * Prints a line to paste into the Artemisia settings tracker.
#
# Usage: sudo env/setup_env.sh [cpu_list]             # apply (saves state first)
#        sudo env/setup_env.sh --restore [cpu_list]   # restore saved state
#        env/setup_env.sh --status [cpu_list]         # show current state (no root)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STATE=$ROOT/env/state/setup_env_saved.txt
MODE=apply
case "${1:-}" in --restore) MODE=restore; shift;; --status) MODE=status; shift;; esac
CPUS=${1:-0-27,56-59,61-67,69-83}     # socket 0; CPUs 60,68 are offline

expand() { local out=() r; IFS=, read -ra parts <<< "$1"; for r in "${parts[@]}"; do
  if [[ $r == *-* ]]; then out+=($(seq "${r%-*}" "${r#*-}")); else out+=("$r"); fi; done; echo "${out[@]}"; }
LIST=$(expand "$CPUS")
cf() { echo /sys/devices/system/cpu/cpu$1/cpufreq; }
misc() { rdmsr -p "$1" -c0 0x1a0; }   # IA32_MISC_ENABLE; bit 38 = turbo disable

status() {
  local on=0 off=0 c; for c in $LIST; do [ -e /dev/cpu/$c/msr ] || continue
    (( $(misc $c) >> 38 & 1 )) && off=$((off+1)) || on=$((on+1)); done
  local c0=${LIST%% *}
  echo "cpus=$CPUS turbo_disabled_bit: set=$off clear=$on | cpu$c0 min=$(cat $(cf $c0)/scaling_min_freq) max=$(cat $(cf $c0)/scaling_max_freq) gov=$(cat $(cf $c0)/scaling_governor) | global no_turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo) (not touched)"
}
log() { echo "TRACKER: $(date '+%F %H:%M %Z') | $(id -un) | artemisia | $1 | CPUs $CPUS"; }

if [ $MODE = status ]; then status; exit 0; fi
[ "$(id -u)" -eq 0 ] || { echo "must run as root (sudo)" >&2; exit 1; }
modprobe msr

if [ $MODE = apply ]; then
  [ -e "$STATE" ] && { echo "saved state already exists ($STATE): restore first" >&2; exit 1; }
  mkdir -p "$(dirname "$STATE")"
  for c in $LIST; do [ -e /dev/cpu/$c/msr ] || continue
    echo "$c $(misc $c) $(cat $(cf $c)/scaling_min_freq) $(cat $(cf $c)/scaling_max_freq) $(cat $(cf $c)/scaling_governor)"
  done > "$STATE"
  echo "saved original state of $(wc -l < "$STATE") CPUs -> $STATE"
  for c in $LIST; do [ -e /dev/cpu/$c/msr ] || continue
    v=$(( $(misc $c) )); wrmsr -p "$c" 0x1a0 $(( v | (1<<38) ))      # turbo off
  done
  cpupower -c "$CPUS" frequency-set -g performance -d 2000MHz -u 2000MHz >/dev/null   # pin 2.0 GHz
  status
  log "Turbo OFF (IA32_MISC_ENABLE[38]=1) + freq pinned 2.0 GHz"
  echo "verify with: env/verify_env.sh <cpu>"
else
  [ -e "$STATE" ] || { echo "no saved state ($STATE)" >&2; exit 1; }
  while read -r c m lo hi gov; do
    wrmsr -p "$c" 0x1a0 $(( m ))
    cpupower -c "$c" frequency-set -g "$gov" -d "${lo}kHz" -u "${hi}kHz" >/dev/null
  done < "$STATE"
  mv "$STATE" "$STATE.restored_$(date +%Y%m%d-%H%M)"
  status
  log "RESTORED original Turbo/freq settings"
fi
