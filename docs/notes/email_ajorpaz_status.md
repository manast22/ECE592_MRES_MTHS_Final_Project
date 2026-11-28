# Email to Dr. Ajorpaz — status, the missing 20k state, accelerator access

**Subject:** ECE 592 project update: AMX ladder reproduced (no 20k state), and a request for QAT/DSA access

Hi Dr. Ajorpaz,

Here's a quick update on our power-gating timing-channel project on artemisia,
plus a request for access to the off-core accelerators.

**What we've done**

I lock the frequency at 2.0 GHz with turbo off for each run. Then I idle AMX
for a controlled time (100 ns to 10 s) and time the next matrix instruction.
I've also measured package power with a paired A/B method across 15 cores.

- **AMX:** I reproduced four of GateBleed's five wake-up levels, on two
  cores. The first instruction costs 135 cycles warm and 682 cycles after
  316–365 ns of idle. After about 3 µs it costs 7.7k cycles (briefly), then
  5.7k cycles from about 5 µs onward. The idle thresholds weren't in the
  paper. We also found the policy adapts:
  - If the core sleeps instead of spinning, AMX stays shallower for longer.
  - If AMX is used frequently, it never reaches the 5.7k state.
- **AVX-512:** Only bits above 128 are gated, in two small steps (~110 and ~200
  cycles). 256-bit and 512-bit pay the same penalty, so they share one gate.
- **Power:** Gating AMX saves only about 0.09 W per core while the latency
  changes by over 5,000 cycles. Our first estimate of the leakage figure of
  merit is about 22 bits per watt for AMX.

**Why we can't reach the ~20k state**

I never see GateBleed's deepest level (~20k cycles). I tried idles up to 10 s,
sleeping instead of spinning, and releasing the tiles first. Here is why I think
it's missing:

- **The BIOS only allows C1.** The OS idle driver is `acpi_idle`, not
  `intel_idle`, and it offers only POLL and C1. Core C6 is never requested,
  and core and package C6 residency both read zero. If ~20k is the cost of
  waking AMX from core C6, this server can't produce it.
- **The settings look like a performance BIOS profile.** C1E is off and the
  uncore is pinned at 2.2 GHz.
- **Ruled out by test:**
  - Setting EPB from 6 to 15 (power-saving) changed nothing.
  - Usage history doesn't block it: the long sweeps leave 0.17–10 s between
    operations.
  - The 1 kHz timer tick doesn't fit the pattern of the 9.4k state.
- **A 9.4k level appeared in one session only** (the first one, 11:06), and
  never again that day. Same binary, same settings, no reboot, and the same
  socket power. That one is still unexplained.

If C6 is the cause, we'll report the ladder as depending on C-states. Is it
possible to enable core C6 on artemisia briefly, or is there another Sapphire
Rapids machine with deep C-states we could use for an hour?

**Access request for Phase 3 (off-core accelerators)**

The next phase compares AMX with the off-core accelerators. They're all present
and driver-bound on artemisia, but our account can't open them. Could you ask
the admin for the following?

1. **`qat` group membership:** for the QAT VFs (`/dev/vfio/*`). QAT is our
   main target because it has power management enabled (`pm_idle_enabled=1`).
2. **`dsausers` group membership:** for `/dev/dsa/*` and `/dev/iax/*`. This is
   the fallback target if QAT doesn't work out.
3. **Read access to QAT `pm_status`:** it's in debugfs, which only root can
   read. Either read access, or a sudoers rule for a read-only logger script,
   would give us the device's actual power state to check our timing against.

The account is `[username]`. Optional and lower priority: an ACL for
`/dev/dlb0` (DLB).

Thanks,
[your name]
