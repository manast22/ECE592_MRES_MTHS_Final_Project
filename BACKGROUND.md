# Background: Power-Gating Side Channels in On-Core vs. Off-Core Accelerators

> Project log / reference doc. Captures the motivation, prior work, and core
> hypothesis for our final project. See `PLAN.md` for the action plan.

## 1. Motivation

As AI inference power grows, accelerators are being pulled onto the CPU die.
Intel AMX (Advanced Matrix Extensions) debuted on 4th-gen Xeon Scalable
(Sapphire Rapids) as an **on-core** matrix unit. To stay within power limits,
these units are aggressively **power-gated** — shut down when idle, woken on
demand. Waking costs latency, and that wake-up latency is observable. GateBleed
(Kalyanapu et al., MICRO 2025, NCSU/Intel) showed this turns a power
optimization into a high-resolution timing side/covert channel.

Our project asks whether this vulnerability class extends to Intel's **off-core**
on-die accelerators (DSA, QAT, DLB, IAA), and how the latency-vs-power tradeoff
differs between on-core and off-core designs.

## 2. Prior work: GateBleed (what it established)

- **Target:** Intel AMX, an on-core accelerator (instructions execute in the
  core pipeline).
- **Signal:** The latency of a single `TDPBSSD` (signed int8 matrix multiply)
  depends on *reuse distance* — the time since the unit was last used.
- **Power-state ladder:** Five distinct latencies were observed —
  approximately **50 / 600 / 6,000 / 9,000 / 20,000 cycles** — labeled Warm
  State + Cold States 1–4.
- **Why AMX is a strong channel:** warm→cold latency ratio is ~400×. By
  comparison, AVX-512's power-gating penalty is only ~150 cycles vs. AMX's
  ~20,000.
- **Power delta is small:** package power measured ~142.08 W (warmest stage)
  vs. ~138.49 W (deepest cold stage) — only ~3.6 W across a 400× latency swing.
- **Covert channel:** ~0.07 bps over a 1-hop Ethernet link on a production
  network — reported as ~70,000× the rate achievable via AVX-512, and resilient
  under ~30,000-cycle noise where AVX-512 accuracy collapses.
- **Demonstrated uses:** training-set membership inference, mixture-of-experts
  routing inference, early-exit detection — timing-only, without logits or
  confidence scores.
- **Mitigations and their tradeoffs (power / performance):**
  - Always-warm (fixed Gate 0): ~12% power, ~0 latency.
  - Fixed Gate-1: ~8.1% power, ~2.5% latency.
  - Fixed Gate-2: ~5% power, ~11.1% latency.
  - Always-cold (Gate 4): ~0 extra power, ~35% performance penalty.
  - Reset on context switch (`TILERELEASE`): <2% overhead below ~10 switch/s;
    ~11.6% power + ~30% performance at ~1,000 switch/s.
  - Also proposed: compiler-inserted dummy AMX ops on leaky paths; microcode
    giving TMUL fixed latency.

**Gaps GateBleed left open (our opportunity):**
- Exact *idle-interval thresholds* that trigger each state transition were not
  published — only the latency stages themselves.
- Off-core accelerators were noted as architecturally different but not
  characterized for this channel.

## 3. On-core vs. off-core: the architectural distinction

This distinction drives the entire project design.

| | On-core (AMX, AVX-512) | Off-core (DSA, QAT, DLB, IAA) |
|---|---|---|
| Execution | In the core pipeline | Separate on-die IP block |
| Invocation | Synchronous instruction | Async descriptor submit (ENQCMD / MOVDIR64B) to a memory-mapped work queue |
| Completion | Instruction retires | Poll a completion record / interrupt |
| Observed latency | ~Just the unit (+ wake) | Submit + queue + DMA + completion **plus** any wake penalty |
| Timing observability | High (one timed instruction) | Low (wake signal buried in end-to-end latency) |

**Core hypothesis:** off-core accelerators save more power when idle (their
design purpose) but leak the power-gating state *less* observably, because the
wake penalty is hidden inside asynchronous end-to-end latency rather than
exposed in a single timed instruction. On-core trades power efficiency for
timing observability; off-core does the reverse.

## 4. Instrumentation advantage: QAT exposes its power state

QAT is the best-instrumented off-core target because it reports its own
power-management state — giving ground truth that AMX never does:
- Power states (idle / initialized / active) are configurable in
  `/etc/4xxx_devX.conf`; PM idle support is enabled by default.
- Current state is readable at
  `/sys/kernel/debug/qat_4xxx_<Domain:BDF>/pm_status`.

This lets us correlate measured latency against the device's self-reported
state and toggle PM on/off to isolate its latency contribution. DSA is the
off-core unit most likely to be *enabled* on available hardware (good for
availability even if less self-instrumented).

## 5. Research questions

- **RQ1 — Power-state ladder (reverse engineering):** Does each unit step
  through discrete latency states with idle time, like AMX? Map state count,
  per-state penalty, and the **idle-timeout thresholds** triggering each
  transition (the data GateBleed did not publish).
- **RQ2 — Latency ↔ power:** Correlate latency penalty against actual power
  delta (RAPL) for each unit. Quantify energy-per-op per state.
- **RQ3 — Residual state (stretch):** Across a gate→wake cycle or a
  security-domain switch, is internal state reset, and does that create a
  cross-domain timing signal?

## 6. Unifying metric

Leakage efficiency (figure of merit) to place all units on one axis:

```
FoM = (distinguishable latency states, in bits) / (power saved in deepest gate, W)
```

High FoM = leaks a lot per watt saved (expect AMX high). Low FoM = power
savings that buy an attacker little (our prediction for off-core).

## 7. Scope caveats

- **"Leftover data" ≠ reading another tenant's matrix.** GateBleed leaks prior
  *use timing*, not prior *data*. The realistic, measurable form of RQ3 is
  whether power-gating residency resets across a context/domain switch and
  whether that is observable. Frame as a question, not a promised result.
- **Frequency scaling is a confounder.** DVFS-based leakage (Hertzbleed) can
  masquerade as power-gating. Pin frequency and disable turbo before attributing
  anything to gating.

## 8. Hardware reality check

- **AMX:** easy — any Sapphire/Emerald Rapids or Granite Rapids, incl. cloud
  VMs (e.g., AWS c7i/m7i are Sapphire Rapids). GateBleed replication works on a
  VM.
- **DSA / QAT / DLB:** hard — per-SKU features, often ship disabled, some need
  Intel On-Demand (SDSi) activation, and are frequently **not exposed inside
  virtualized instances** (typically need bare metal with the accelerator
  enabled and passed through). Confirm availability before committing to
  off-core measurements.

## 9. Key references

- Kalyanapu, Dizani, Asher, Ghanbari, Cammarota, Aysu, Mirbagher Ajorpaz.
  "GateBleed: Exploiting On-Core Accelerator Power Gating for High Performance
  and Stealthy Attacks on AI." MICRO 2025. arXiv:2507.17033.
- Wang et al. "Hertzbleed: Turning Power Side-Channel Attacks Into Remote Timing
  Attacks on x86." USENIX Security 2022. (DVFS/frequency confounder.)
- Intel QAT programming guide — Power Management (sysfs `pm_status`,
  `/etc/4xxx_devX.conf`).
- Intel DSA / `idxd` driver + `accel-config` documentation.
