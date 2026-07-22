# U2+L debugger: cartridge NMI is not forwarded by the C64U host

**Status:** blocking for U2 debug stepping *on a C64 Ultimate (C64U) host*. Not a
U2+L firmware bug. Very likely works on a real breadbin C64. Investigated and
proven on real hardware 2026-07-21 (U2+L `192.168.1.97` in C64U `192.168.1.167`).

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
natively drives the 6510, so the debugger should work there.

## Fix location

The fix belongs in the **C64U / Ultimate-64 core** (route the incoming cartridge
NMI to the internal 6510 NMI, honoring `Bus Sharing - Interrupts`), not in the
U2+L firmware. Cannot be built/validated from this repo's U2+L toolchain.

## U2+L firmware fixes made here (correct + necessary once the NMI is delivered)

These were found while root-causing and are kept; they are prerequisites for the
launch to work on any host that *does* forward the NMI (e.g. a real C64):

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
`tools/developer/machine-code-monitor/` for the split-session driver
(`--c64-host`) used to reach the U2+L overlay from the U2 while sending keys /
memory / reset to the C64U.
