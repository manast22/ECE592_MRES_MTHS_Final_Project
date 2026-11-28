# Question emails: the 9.4k and ~20k AMX states

Fill in the `[...]` fields. Both emails can go to a professor/TA or to the
GateBleed authors. Email 2 is written mainly for the GateBleed authors.

---

## Email 1 — the 9.4k-cycle state that appeared once and never again

**Subject:** AMX power-gating: a deep 9.4k-cycle state that showed up in one session only

Hi [name],

I'm an ECE 592 student measuring the AMX power-gating timing channel that
GateBleed reported on Sapphire Rapids. I've reproduced most of it, but one state
behaves strangely and I'd value your read on it.

**Setup.**
- Machine: 2x Xeon Gold 5420+ (Sapphire Rapids), Lenovo ThinkSystem SR650 V3 (BIOS ESE136G-3.70), microcode 0x2b000685, RHEL 9 with kernel 6.17. It's a shared server, not rebooted since 26 Oct.
- Power settings: EPB = 6 on all cores, uncore pinned at 2.2 GHz (MSR 0x620 = 0x1616), and C1E off. The idle driver is `acpi_idle` with only POLL and C1, so core C6 is never requested.
- Frequency: turbo off per core (IA32_MISC_ENABLE bit 38), and min = max = 2.0 GHz, so TSC ticks equal core cycles.
- Pinning: one test core (CPU 10), with its sibling and neighbors otherwise idle.
- Method: spin for a chosen idle time Δ (100 ns to 10 s, log-spaced, 50 trials each, shuffled). Then time one `TDPBSSD` + `TILESTORED` with `LFENCE;RDTSC;RDPMC;LFENCE`.
- Code: `bench/amx_probe.c`; timer in `bench/common/timer.h`.

**What reproduces (3 sessions, 2 cores):**

| Idle time Δ | Latency of the next TDPBSSD |
|---|---|
| short (warm) | 135 cycles |
| from 316–365 ns | 682 cycles |
| 1.8–3.2 µs | ~7.7k cycles (transient) |
| 3.2–5.6 µs to 10 s | 5.7k cycles |

**The problem.** In the first session (11:06), idles of 100 µs and longer moved to
a **9.4k-cycle** level. In every later session that day it never appeared.
Same binary, core and settings. Data: `data/raw/slot_artemisia_20261127-1106/`.

**What I ruled out** (all runs still gave 0 % of samples at 9.4k):
- The SMT sibling spinning.
- 4 neighbor cores spinning.
- 4 neighbor cores running AMX.
- The previous sample's idle time (checked by ordering the samples by time).
- Time since the process started.
- Releasing the tiles (`TILERELEASE`) before the idle. This added some 9.5k samples at the tail, but they're not a stable level.

Package and core C6 residency both read 0 here; the only idle states exposed are POLL and C1.

I also ruled out the following:
- **No settings change between sessions.** The server was not rebooted, and tuned is inactive (its profile is unchanged since boot).
- **Not the uncore frequency.** The uncore is pinned at 2.2 GHz.
- **Not other load on the socket.** Package power was the same in session 1 and session 2 (median 102.6 vs. 102.5 W).
- **Not the timer tick.** In session 1 the share of 9.4k samples jumps from 2 % to 45 % at 100 �s and levels off around 80�85 %. If it were caused by the 1 kHz tick, the share would rise in proportion to idle time and reach 100 % at 1 ms.

**Questions:**
1. Have you seen AMX wake-up latency depend on package-level or socket-wide
   state, such as other tenants or a package C-state that isn't reported?
   The uncore is pinned here, so it can't be an uncore frequency change.
   Could 9.4k be 5.7k plus some other mesh wake-up?
2. Is there an MSR or counter that would show which state AMX is in, or whether
   the core was "deeper" at 11:06? I can read MSRs with root during booked
   sessions.
3. Is a once-only deep state worth chasing, or is a shared server too noisy and
   I should report it as non-reproducible?

The full notes are in `docs/notes/2026-11-27_turbo_off_slot3.md`, and the
detection script is `analysis/processing/find_thresholds.py`.

Thanks,
[your name]

---

## Email 2 — no ~20k-cycle state anywhere, and the policy changes with usage

**Subject:** GateBleed AMX ladder: we never see the ~20k-cycle state — how was it reached?

Hi [name],

I'm reproducing GateBleed's AMX state ladder (~50 / 600 / 6k / 9k / 20k cycles)
on a Xeon Gold 5420+ for a course project. I find four of the five levels, but
never the ~20k one. I'd like to know what conditions your measurement used.

**What we measured** (turbo off, fixed 2.0 GHz, cycles via RDPMC; code
`bench/amx_probe.c`):
- **Spin idle up to 10 s:** 135 → 682 → 7.7k → 5.7k. The deepest is 5.7k
  (9.4k once, in one session). No 20k.
- **Sleep idle** (`clock_nanosleep`, so the core halts in C1): ~730 cycles up to
  0.57 ms, 3.3k from 1 ms, and 5.7k only from 0.18 s. No 20k.
- **Tiles released** (`TILERELEASE` before the idle; we then time `LDTILECFG`
  + loads + op + store): the same steps, slightly shifted (warm 362, then
  875 / 7.6k / 5.9k). No 20k.
- **The policy adapts to usage.** If all idles in a run are short (≤ 10 µs),
  AMX stays at ~720 cycles and never reaches 5.7k. If they are 30 µs–1 ms, it
  settles at 3.3k. A new test that times every op during a steady
  power-measurement run (`bench/power_ab.c lat_out=`) agrees: one op every
  20 µs stays at ~640 cycles. This was a quick check with turbo on; the
  turbo-off run is pending.

**Our platform limits:**
- Only POLL and C1 are exposed. Package and core C6 residency are both 0.
- No energy-cores RAPL counter.
- The server is shared.

**Questions:**
1. In GateBleed, under what conditions did the ~20k state appear: idle length,
   how the core idled (spin, sleep, or deeper C-states), and core vs.
   package C6?
2. Could 20k require core C6 (losing AMX state/power entirely)? This
   server's BIOS exposes only C1 to the OS (`acpi_idle`; EPB = 6), so the
   core never enters C6. If so, should I report the ladder as
   "C-state-dependent" rather than a fixed 5 levels?
3. Did you see the controller adapt to how often AMX is used (shallower states
   under frequent use)? This seems important for whether the channel works in
   practice, so I'd like to know if it matches your results.

Thanks for any pointers,
[your name]
