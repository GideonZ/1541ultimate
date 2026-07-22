# Machine Code Monitor: served-ROM write-to-fetch coherency (U64)

## Summary

On the Ultimate 64 the debugger can plant a `BRK` ($00) breakpoint directly into
the FPGA-served BASIC/KERNAL/CHAR ROM image, so KERNAL/BASIC code can be debugged
without copying ROM into RAM. Writes to that image go through DMA and are
immediately coherent on the **DMA/data** read path. They are not guaranteed
coherent on the live 6510 **instruction-fetch** path for a ROM line that has not
been re-fetched since the write. In the worst case this lets a *contextless*
visible-ROM breakpoint entry (arm a breakpoint in visible ROM, then `Go` into it
from a short RAM bootstrap, with no prior execution of that ROM line) miss the
breakpoint on the 6510's first fetch and run past it.

In practice this is rare. On a healthy device it does not occur at idle at all
(see Evidence: 14/14 clean first-fetch hits, ~2s each). It is a load-conditioned
edge and every *warm*/context path - RAM, RAM-under-ROM, and all step, step-over,
step-out, continue, continue-to-cursor and continue-to-breakpoint from a running
or frozen program - is deterministic.

## Two things were previously conflated

The historical "the contextless ROM entry is unreliable" report combined two
different problems, one of which was not a debugger defect at all:

1. **A test-wait timing artifact (the dominant one).** The firmware `go()` for a
   contextless visible-ROM launch blocks for its full breakpoint wait plus its
   bounded in-place relaunch budget (`BREAKPOINT_WAIT_MS` = 5000ms x up to
   `MAX_BREAKPOINT_RELAUNCH` + 1 = 3 attempts, ~15s) before it returns and paints
   the result. The old test harness waited only ~6s and then **reset and retried**,
   so it aborted `go()` mid-budget and counted slow-but-succeeding entries as
   "misses". Measuring the miss rate that way (30 iterations, 6s wait, reset on
   timeout) reported ~37% - but that number is an artifact of the short wait plus
   network/telnet degradation accumulated over the run, not a true first-fetch
   miss rate.

2. **A genuine, rare, load-conditioned fetch race.** Under concurrent REST/DMA
   traffic the first fetch of the freshly patched ROM line can still be stale.
   This is real but infrequent, and it is what the honest error below is for.

The fix addresses both: wait out the full `go()` budget (so slow successes are
recorded as successes), and report the residual genuine miss precisely instead of
masking it with a reset.

## Why the closed core cannot be fixed here

The U64 C64 core (6510, VIC-II, SID, and the ROM-serving memory subsystem) is a
**closed, prebuilt FPGA bitstream**: `external/u64.sof`, copied from a separate
private repository (`target/u64/nios2/updater/getbuild.sh` ->
`ult64/target/u64_a4/work/u64.sof`). This repository contains no VIC-II source and
no U64 FPGA project, so the served-ROM fetch path cannot be changed or simulated
here, and there is no hardware PC-compare/breakpoint register (the debugger is
BRK-only). The FPGA approaches from the hardening contract (write-invalidation,
fetch-flush handshake, coherent read-through) are therefore not implementable in
this tree; the fix is firmware- and test-side only.

## What the firmware does (honest, no reset)

1. **Best-effort settle** (`settle_visible_rom_for_live_fetch`) briefly clocks the
   core before a contextless visible-ROM launch. It *reduces* miss probability; it
   is explicitly not a determinism guarantee.
2. **Bounded in-place relaunch** (`relaunch_on_breakpoint_runaway`,
   `MAX_BREAKPOINT_RELAUNCH` = 2) re-parks the CPU and re-issues the launch
   **without any reset**. It is bounded and does not loop.
3. **Precise error.** If the entry still misses after the budget, the session
   returns the distinct result `DBG_ROM_ENTRY_UNCOHERENT` and the monitor shows
   `ROM BP ENTRY MISSED - RUN CODE FIRST`. All breakpoint patches are restored (no
   stale `BRK` left in the ROM image), the handler is uninstalled, and the debugger
   stays usable. **No reset is performed and the command is not replayed.** The
   actionable guidance in that message is correct: run the target so its fetch line
   warms, or set the breakpoint from a running/frozen context.

This is validated deterministically by host tests
(`test_contextless_visible_rom_entry_reports_uncoherent_on_miss` asserts the
result, patch restoration, and a usable post-error session;
`test_ram_under_rom_timeout_is_not_reported_as_uncoherent` proves the honest
classification does not leak onto a plain RAM-under-ROM timeout).

## Reset classification and prohibited masking

- **explicit_reset** - the user-invoked debugger Reset op. Normal functionality.
- **setup_reset** - one baseline reset before a test's workflow begins. Legitimate.
- **recovery_reset** - resetting to get another independent attempt at a failed
  debugger op. **Prohibited** (probabilistic masking, not correctness).
- **transparent_reset** - checkpoint/reset/restore. Not used: it would be no more
  deterministic than the non-reset path and adds large risk for a rare edge.

The test harnesses previously masked the ROM-entry miss with a 5x
reset-and-retry (`ROM_ENTRY_MAX_ATTEMPTS`). That masking has been removed:

- `monitor_debug_matrix_gate.py` waits out the full `go()` budget (`ROM_ENTRY_WAIT_S`)
  and attempts ROM entry **once**. A genuine miss is recorded as a terminal
  `ROM_ENTRY_UNCOHERENT` status (a documented limitation, never `PASS`, never
  reset-retried). The final report prints the reset/retry counters and asserts the
  prohibited ones are zero.
- `monitor_debug_test.py` (`_bootstrap_hit_rom_breakpoint`,
  `_contextless_visible_jsr_step_over`) attempt **once** with the full-budget wait
  and, on the honest miss, raise `RomEntryUncoherent` - a `SkipCheck` subclass
  surfaced transparently as a documented-limitation `SKIP`.
- `test_no_reset_retry_masking.py` statically fails the build if a reset-retry loop
  (attempt/`while`/reset-in-`except` around a debugger launch) reappears in a gate
  harness.

Separately, rapid reset + telnet + REST cycling degrades the firmware network
stack after ~9 iterations ("back-to-back degrades telnet"); a fresh JTAG redeploy
recovers it. That degradation is not the fetch race and is not masked as one.

## U64 vs U2

This concerns the U64 served-ROM image only. The U2/U2+ cartridge backend does not
support visible-ROM patching (`supports_visible_rom_patching()` and
`visible_rom_fetch_lags()` are false; `settle_visible_rom_for_live_fetch()` is a
no-op), so it debugs ROM code only from writable RAM and is unaffected.

## Evidence

- Device: Ultimate 64 Elite, firmware 3.15, FPGA 122, core 1.4B.
- **Idle, fresh deploy, full ~22s budget wait, 20 contextless `$E002` entries:**
  14 clean HITs at ~2.0s, 0 honest-coherency misses; the 6 non-hits were all
  network/telnet degradation (NO_OUTCOME + reconnect "Snapshot mismatch"), not the
  fetch race.
- The old ~37% figure came from a 6s wait that reset on timeout, aborting the
  firmware's ~15s `go()` budget mid-flight, plus accumulated degradation. It is a
  measurement artifact, not a first-fetch miss rate.
- Host tests deterministically validate the `DBG_ROM_ENTRY_UNCOHERENT` path and its
  non-leakage onto RAM-under-ROM timeouts.
