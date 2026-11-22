#!/usr/bin/env bash
# Snapshot of the test host's CPU, accelerator, power and frequency config.
# Usage: env/collect_hostinfo.sh > env/hostinfo_$(hostname -s)_$(date +%F).txt
# Re-run before every measurement campaign; commit the output with the data.
set -u
sec() { printf '\n===== %s =====\n' "$1"; }

sec "date / host";      date -Is; hostname -f; uname -r; grep PRETTY /etc/os-release
sec "cpu";              lscpu | grep -E 'Model name|Socket|Core\(s\)|Thread|NUMA node|On-line|Off-line|L[123]'
                        grep -m1 microcode /proc/cpuinfo
sec "isa flags";        grep -m1 -o -w 'amx[a-z_0-9]*\|avx512[a-z_0-9]*\|enqcmd\|movdir64b\|movdiri\|waitpkg\|serialize\|constant_tsc\|nonstop_tsc\|tsc_known_freq' /proc/cpuinfo | sort -u | tr '\n' ' '; echo
sec "kernel cmdline";   cat /proc/cmdline
sec "isolated / nohz";  echo "isolated=$(cat /sys/devices/system/cpu/isolated) nohz_full=$(cat /sys/devices/system/cpu/nohz_full)"
sec "cpufreq";          c=/sys/devices/system/cpu
                        echo "driver=$(cat $c/cpu0/cpufreq/scaling_driver) governor=$(cat $c/cpu0/cpufreq/scaling_governor)"
                        echo "scaling_min=$(cat $c/cpu0/cpufreq/scaling_min_freq) scaling_max=$(cat $c/cpu0/cpufreq/scaling_max_freq)"
                        echo "intel_pstate status=$(cat $c/intel_pstate/status) no_turbo=$(cat $c/intel_pstate/no_turbo) min_pct=$(cat $c/intel_pstate/min_perf_pct) max_pct=$(cat $c/intel_pstate/max_perf_pct)"
sec "cpuidle";          echo "driver=$(cat $c/cpuidle/current_driver) governor=$(cat $c/cpuidle/current_governor)"
                        for s in $c/cpu0/cpuidle/state*; do echo "$(basename $s) $(cat $s/name) lat=$(cat $s/latency)us disable=$(cat $s/disable)"; done
sec "rapl";             for z in /sys/class/powercap/intel-rapl:*; do echo "$(basename $z) $(cat $z/name) readable=$([ -r $z/energy_uj ] && echo y || echo n)"; done
sec "perf";             echo "perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid) nmi_watchdog=$(cat /proc/sys/kernel/nmi_watchdog)"
sec "accelerators (PCI)"; lspci -nn | grep -E '8086:(0b25|0cfe|4942|4940|4944|2710)\]' 
sec "idxd devices";     for d in /sys/bus/dsa/devices/{dsa,iax}[0-9]*; do [ -e "$d/state" ] && echo "$(basename $d) state=$(cat $d/state) numa=$(cat $d/numa_node)"; done
                        for w in /sys/bus/dsa/devices/wq*; do [ "$(cat $w/state 2>/dev/null)" = enabled ] && echo "$(basename $w) mode=$(cat $w/mode) name=$(cat $w/name) size=$(cat $w/size)"; done
sec "qat";              for p in /sys/bus/pci/devices/*/qat; do echo "$(basename $(dirname $p)) state=$(cat $p/state) services=$(cat $p/cfg_services) pm_idle_enabled=$(cat $p/pm_idle_enabled)"; done
                        echo "qatmgr: $(pgrep -a qatmgr || echo not running)"
sec "device access for $(id -un)"
                        for f in /dev/dsa/* /dev/iax/* /dev/dlb* /dev/vfio/[0-9]* /dev/cpu/0/msr /sys/kernel/debug; do [ -e "$f" ] || continue; printf '%-22s %s\n' "$f" "$([ -r "$f" -a -w "$f" ] && echo RW || ([ -r "$f" ] && echo "R (file mode only)" || echo none))"; done
                        echo "(MSR open() also needs CAP_SYS_RAWIO, so R above does not mean rdmsr works)"
sec "tools";            for t in gcc clang perf turbostat cpupower pqos stress-ng accel-config cpa_sample_code qatzip-test python3; do printf '%-16s %s\n' $t "$(command -v $t || echo MISSING)"; done
                        gcc --version | head -1; python3 --version
