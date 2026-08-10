# U2+L debugger: cartridge NMI is not forwarded by the C64U host

**Status:** blocks the debugger's `G`/entry launch *on a C64 Ultimate (C64U)
host*. This is a limitation of the host, not the cartridge. Behaviour in a real
C64 has not been tested here - no U2+L has been run in one during this work, so
treat a real C64 as untested rather than as known-good.
Investigated and proven on real hardware 2026-07-21 (U2+L `192.168.1.97` in C64U
`192.168.1.167`), re-confirmed with a control 2026-08-08 (see below).

## Scope: what still works

This limitation is narrower than "the U2+L debugger is unusable on a C64U host".
Measured on real hardware 2026-08-08 (U2+L `192.168.1.99` in C64U
`192.168.1.148`), all of the following work through the cartridge's own overlay
UI:

- `D` enters Debug mode and the register footer appears.
- `R` arms a breakpoint; the disassembly row shows its `[BRKn]` marker.
- `G` launches the program. On the U2+L that goes through the boot cartridge
  (`monitor_io::jump_to` -> `C64_DMA_BUFFER` / `RUNCODE_DMALOAD_JUMP`), which
  resets the C64 first, so it does not need the cartridge NMI at all. A program
  placed below the area the reset clears is destroyed by its own launch; one at
  `$1000` or above runs.
- The running 6510 reaches the breakpoint and traps. The overlay reappears with
  the PC row highlighted, a populated `PC/AC/XR/YR/SP/NV-BDIZC/IRQ/NMI` footer,
  and the source tag on the PC row switched from `[CPU]` to `[RAM]`.

So breakpoint-driven entry is fine. What the missing NMI costs is redirecting an
already-running 6510 to a chosen PC without going through a breakpoint.

## Re-confirmed 2026-08-08, with a control

The earlier finding was re-measured on a U2+L (`192.168.1.99`) in a C64U
(`192.168.1.148`), with the control that distinguishes this from a broken launch
mechanism. Same program (`INC $C020` / `JMP $C000` at `$C000`), same address,
same `G`:

| | `$C020` | target ran? | jiffy | 6510 alive? |
| --- | --- | --- | --- | --- |
| plain `G`, no debug session | `$B6` | yes | `C7` -> `0A` | yes |
| same `G`, debug session active | `$00` | no | `5A` -> `70` | yes |

The control is what makes this diagnostic: the launch path itself works. Three
alternatives are closed by those two measurements - the 6510 is not wedged
(jiffy advances), the program is not running-but-untrapped (its own `INC` never
executed), and the launch mechanism is not broken in general (the control ran).

**Signature of this limitation**, for classifying a failing check as belonging
to it rather than merely failing near it:

- the target's own write never happens (its sentinel byte is unchanged)
- the jiffy clock keeps advancing (the 6510 is alive)
- no trap, and the debug PC never moves into the target

A check that fails in some other way is an ordinary failure that happens to sit
in the same area, and must not be attributed here.

### How many checks this blocks: an upper bound, not a count

Counting the checks that *call* an execution-dependent helper
(`_wait_for_pc`, `_wait_for_pc_register`, `_enter_rom_debug_at`,
`_bootstrap_hit_rom_breakpoint`, `_step_and_assert_pc`, `goto_run` inside Debug)
gives **at most 24** not already skipped for other reasons. That is an upper
bound derived from what each check calls. It is **not** a count of checks this
limitation blocks, and it must not be quoted as one.

It is already known to overcount. "Debug: D over without captured context
executes from cursor" is in that set and **passes on this hardware** (measured
2026-08-08, 21.6s). Membership of the set is a property of the code; being
blocked is a property of the hardware, and the two are not the same.

`monitor_debug_test.py` classifies each execution failure at the point it
happens, and only one verdict justifies a skip:

| verdict | meaning | action |
| --- | --- | --- |
| `HOST-NMI-SIGNATURE` | a context was held and the step did not advance it - the debugger had the CPU and could not move it | skippable, with the reason below |
| `AMBIGUOUS` | no context before or after: consistent with the missing NMI on entry, but indistinguishable from any other failure to enter | stays red |
| `NOT-HOST-NMI` | the 6510 is wedged, or the debug PC did move | stays red; an ordinary defect |

`AMBIGUOUS` deliberately does not justify a skip. A skip asserts that a check can
never pass, and evidence that cannot be told apart from an ordinary failure does
not carry that claim. Resolving one needs the third signature element - the
target's own sentinel - which is specific to each check's fixture and so cannot
live in a shared helper.

The reason string to use when a check does qualify:

> the C64 Ultimate host forwards cartridge DMA and reset but not the cartridge
> NMI, so the debugger cannot redirect the running 6510. This is a limitation of
> the host, not the cartridge. Behaviour in a real C64 has not been tested here.

## Separate finding: freezing forces Ultimax, so $D000 lies

Not an NMI issue, recorded here because it is the other place the C64U/freezer
arrangement makes the cartridge see something the 6510 does not.

`stop()` / `backup_io()` force Ultimax so the cartridge can reach the I/O space.
Ultimax maps I/O at `$D000-$DFFF` **regardless of the 6510's port**, so while the
freezer menu is up the cartridge cannot see what the CPU has mapped there.

Measured with a program holding the port at `$33`, which puts the character ROM
at `$D000` for the CPU:

| observation | result |
| --- | --- |
| cartridge reads `$D000` with the menu closed (machine running) | `3C 66 6E 6E 60 62 3C 00` - the character ROM's `@` glyph |
| cartridge reads `$D000` with the menu open (frozen, same port) | `00 00 00 00 00 00 00 00` - I/O |

Two consequences, and the second is a defect:

1. **The CPU bank cannot be fully derived while frozen.** `$A000` and `$E000`
   stay observable, and deriving those two from window contents was verified
   correct across ports `$37/$35/$33/$30/$31`. `$D000` cannot be, so
   `U2MemoryBackend::derive_cpu_port()` refuses while frozen rather than report
   a mapping nobody measured, and the status row falls back to its VIC-only
   form.

2. **The monitor's memory view at `$D000` shows bytes the CPU cannot see.** With
   the CPU mapping the character ROM there, the monitor displays the VIC
   registers instead, with no indication:

   ```
   what the CPU sees at $D000 : 3C 66 6E 6E 60 62 3C 00
   what the monitor shows     : 00 00 00 00 00 00 00 00
                                00 1B 42 52 42 00 C8 00    <- $D011 = $1B, a VIC register
   ```

   This is the same family as a breakpoint marker that is not drawn when the
   slot's store disagrees with the view: the instrument is confidently wrong
   rather than silent. Logged, not fixed.

   **Pre-existing, not introduced by the debugger branch.** At the merge base
   `8efdffe6`, `U2MemoryBackend::read` and `read_block` already reached this
   memory through `machine->peek(address)`, freezing already forced Ultimax, and
   the U2 monitor already had to freeze in order to read. The frozen `$D000`
   read path is unchanged from the base commit: `ULTIMAX_HIDES_MEMORY` covers
   `$1000-$CFFF` and `$E000+`, deliberately excluding `$D000-$DFFF`, so that
   range takes the same plain path it always did. Note this is a comparison of
   the code against the base commit, not a hardware A/B against a base build.

   The debugger's next-to-execute row is a separate question and does **not**
   have this problem: it is only drawn when a valid debug context is held
   (`debug.is_active() && debug.has_context() && debug.context().valid`), and on
   the U2+L a context can only arise from a BRK capture, which records the port
   the 6510 itself reported. So that row's tag is derived from an observed port
   rather than from the fallback used when the port is unknown.

The fix for (1) is to capture the answer at freeze time, before Ultimax is
forced - the pattern `backup_io()` already uses for `cia_backup[1]`, which
records CIA2 port A for exactly the same reason. That would also give (2) the
information it needs to warn.

## Symptom

On the U2+L cartridge, the machine-code-monitor debugger's `G`/entry launch never
redirects the C64's 6510 to the target. Enter Dbg, set a breakpoint, `goto`
bootstrap, press `G`: the 6510 keeps running KERNAL/BASIC (jiffy advances) and
never traps, so entry/stepping never completes.

## Mechanism (how the launch is supposed to work)

`monitor_debug_brk_session.cc` redirects the 6510 to a target PC by installing an
NMI trampoline (soft vector `$0318` -> `$03B0` -> restore vector -> `JMP target`)
and pulsing the cartridge NMI (`C64_MODE = C64_MODE_NMI`), so the running 6510
takes the NMI and vectors through the trampoline. This is the same mechanism the
U64 backend uses, where it works (matrix 18/18).

## Root cause (proven)

**`C64_MODE_NMI` never produces a 6510 NMI on the C64U host, in any bus
configuration.** Proven with a UI-independent firmware self-test: install a tiny
handler at `$02D0` (`INC $02FA; PLA*3; RTI`), point `$0318` at it, pulse
`C64_MODE_NMI` while the C64 runs, and read `$02FA` back over DMA - it stays `$00`
(NMI never fired). Verified across every C64U `C64 and Cartridge Settings` combo:

| Bus Operation Mode | Bus Sharing - Interrupts | C64 usable | NMI reached 6510 |
| --- | --- | --- | --- |
| Quiet | Both / External | yes | **no** (`$02FA=00`) |
| Writes | Both | yes | **no** |
| Dynamic | Both / External | yes | **no** |
| Dyn. & Writes | Both | yes | **no** |
| Compatibility | Both / External | no (DMA memory view diverges; overlay UI breaks) | untestable / unusable |

The **U2+L side drives the NMI correctly**: `C64_MODE` bit 4 ->
`control.c64_nmi` (`fpga/cart_slot/vhdl_source/cart_slot_registers.vhd`) ->
`nmin_o` (`slot_server_v4.vhd:1075`, unconditional) -> `oc_pusher` -> `SLOT_NMIn`
(`fpga/fpga_top/ultimate_fpga/vhdl_source/u2p_riscv_lattice.vhd:835`). That is the
*same* register + `oc_pusher` path as `SLOT_DMAn` (DMA-stop, which works) and the
reset path (which works). So the U2+L asserts the cartridge NMI pin fine.

**The gap is in the C64U host:** it forwards cartridge DMA and reset to its
internal (FPGA) 6510, but not the cartridge NMI, and no bus-operation-mode /
interrupt-sharing setting changes that. On a real C64 the cartridge NMI pin
natively drives the 6510, so there is reason to expect the debugger to work
there - but that has not been tested, here or anywhere in this campaign.

## Fix location

The fix belongs in the **C64U / Ultimate-64 core** (route the incoming cartridge
NMI to the internal 6510 NMI, honoring `Bus Sharing - Interrupts`), not in the
U2+L firmware. Cannot be built/validated from this repo's U2+L toolchain.

## U2+L firmware fixes made here (correct + necessary once the NMI is delivered)

These were found while root-causing and are kept; they are prerequisites for the
launch to work on any host that *does* forward the NMI:

1. `software/monitor/monitor_debug_u2.cc` - the U2 backend was missing the
   `request_staged_nmi` / `clear_staged_nmi` overrides the base `BrkDebugSession`
   staged launch relies on (base bodies are empty; the U64 backend overrides
   them). Added, delivering the NMI via a stopped-session resume.
2. `software/io/c64/c64.{h,cc}` - `C64::resume()` wrote `C64_MODE = MODE_NORMAL`
   (clearing the NMI) *before* `C64_STOP = 0` (un-stop), so an NMI asserted while
   stopped was wiped before the CPU un-stopped. Added an `nmi_on_resume` flag and
   `C64::end_stopped_session_nmi()` so `resume()` can keep `C64_MODE_NMI` asserted
   through the un-stop. Guarded (defaults off), U2-family only (U64 uses its own
   machine class).

## Reproduce / verify

Set up `$0318` + a handler via `machine:writemem` on the C64U, pulse
`C64_MODE_NMI` from U2+L firmware while the C64 runs, read the marker back via
`machine:readmem`. See the harness notes and
`tests/e2e/monitor/` for the split-session driver
(`--c64-host`) used to reach the U2+L overlay from the U2 while sending keys /
memory / reset to the C64U.
