#!/usr/bin/env bash
# ROOT-ONLY measurement lockdown for artemisia. We do not have sudo; this script
# documents exactly what to ask the admin to run (or to allow via a sudoers rule).
# Usage: sudo env/setup_env.sh [cpu_list=28-55,84-111]   |   sudo env/setup_env.sh --restore
set -euo pipefail
CPUS=${1:-28-55,84-111}
[ "$(id -u)" -eq 0 ] || { echo "must run as root" >&2; exit 1; }
if [ "${1:-}" = "--restore" ]; then
  echo 0 > /sys/devices/system/cpu/intel_pstate/no_turbo
  cpupower -c all frequency-set -g performance -d 800MHz -u 2000MHz
  for c in $(seq 0 111); do [ -e /dev/cpu/$c/msr ] && wrmsr -p $c 0x1a0 $(( $(rdmsr -p $c -c 0x1a0) & ~(1<<38) )) || true; done
  exit 0
fi
# 1. Turbo off. sysfs no_turbo=1 is already set but the core still runs at
#    ratio 27 (2.7 GHz), so also set IA32_MISC_ENABLE[38] (turbo disable).
echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
modprobe msr
for c in $(seq 0 111); do [ -e /dev/cpu/$c/msr ] && wrmsr -p $c 0x1a0 $(( $(rdmsr -p $c -c 0x1a0) | (1<<38) )) || true; done
# 2. Pin P-state: min = max = base (2.0 GHz == TSC) on the test cores.
cpupower -c "$CPUS" frequency-set -g performance -d 2000MHz -u 2000MHz
# 3. C-states: acpi_idle only exposes POLL/C1 here; leave C1 enabled (AMX gating
#    is a unit-level mechanism, not core C-state) but record the setting.
# 4. Let the user read QAT pm_status: debugfs is root-only (0700).
#    Option A (preferred): admin runs env/qat_pm_logger as a sudoers-allowed cmd.
#    Option B: mount -o remount,mode=0755 /sys/kernel/debug  (system-wide; ask first)
echo "done. verify with: env/verify_env.sh <cpu>"
