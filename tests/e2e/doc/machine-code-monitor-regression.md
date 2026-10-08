# The machine code monitor debugger regression gate

## The suites this gate selects from

The machine code monitor debugger has three registered E2E suites:

| suite | what it runs | measured wall clock on a U64 |
| --- | --- | --- |
| `machine-code-monitor` | 27 checks of the monitor itself, no debugger | 9-27 min |
| `machine-code-monitor-debug` | 21 semantic groups, ~95 checks, Telnet only | 28-50 min |
| `machine-code-monitor-matrix` | 5 memory modes x 3 UI transports x 3 repetitions = 45 cells, plus a closing random-program run | 74-82 min for a clean 45/45 |

Running all three before a merge is over two hours of hardware time. Two of the
three are marked `manual` for that cost.

`machine-code-monitor-regression` is the fourth suite: a selection out of the
other three, chosen so that dropping a lane cannot hide a defect the dropped
lane was the only one able to find. It owns no test logic. Every check it runs
is executed by `monitor_debug_matrix_test.py` or `monitor_debug_test.py`. The
suite is the selection, plus one check nothing else performs (section 6).

Sections 1 to 4 hold the evidence the selection rests on, so a later reader can
challenge it against the same measurements. `--list-plan` prints the selection
itself without touching a device.

## Run it

```bash
./run-tests u64 --manual -s machine-code-monitor-regression
./run-tests u2@c64u --manual -s machine-code-monitor-regression

# the plan and its reasoning, no device involved
python3 tests/e2e/monitor/monitor_debug_regression_test.py \
    --host u64 --rest-host u64 --list-plan
```

The suite is registered `manual` for the same reason `machine-code-monitor-debug`
is: about an hour of hardware time on an Ultimate 64, which is too much to spend
on every sweep. It is the suite to name explicitly before a merge.

## What was measured, and where the evidence came from

Four independent sources, each of which answers a different question.

| source | question it answers |
| --- | --- |
| The firmware, read down from the key dispatcher to the step primitives | Where does the same debugger operation take a different code path depending on the UI? |
| The matrix run ledgers: 36 recorded runs across `doc/research/machine-monitor/matrix-runs/` and `doc/research/machine-code-monitor/matrix-runs/`, plus 189 artifact ledgers | Which cells have failed, how often, and on which repetition? |
| The recorded debugger defects on this branch | Which failure modes exist, and what condition triggers each one? |
| Per-cell phase timestamps in surviving `cell.log` artifacts | What does each lane cost? |

## 1. Where the firmware actually branches on the UI

All three transports reach the debugger through one entry point,
`UserInterface::run_machine_monitor()`. There is no transport enum in the
debugger. The key dispatcher, the five request functions
(`debug_request_over/trace/out/cursor`, `debug_toggle_breakpoint`) and the step
gate contain no transport test at all - `debug_resolve_step()` reads the freeze
flag into `ui_freeze` and then explicitly discards it
(`machine_monitor_debug_impl.inc:569`).

What the transport does decide is three booleans, derived in
`run_machine_monitor.cc:40-48` and at `debug_enter()`:

| flag | Freeze | Overlay | Telnet |
| --- | --- | --- | --- |
| `debug_run_window_refreeze_enabled` (host is the C64 and it is accessible) | yes | no | no |
| `reset_exits_monitor` (direct render target or a permanent host) | yes | yes | no |
| `debug_owner.remote` (the screen prefers a full refresh) | no | no | yes |
| `machine_is_frozen()` | yes | no | no |

Those flags produce eight conditional paths. Five of them do not matter for a
gate. Four do:

| operation | memory mode | does a result transfer between transports? |
| --- | --- | --- |
| breakpoint set/clear/popup/slot table | any | **yes** - no transport input anywhere |
| step gating and refusal policy | any | **yes** - `ui_freeze` is discarded |
| control-flow step (JMP, branch, JSR, RTS, RTI) | any | **yes** - keys only on parked state and banking |
| linear Step Over/Into | RAM, RAM-under-ROM | **yes** |
| linear Step Over/Into | **visible ROM** | **no** - trampoline or interpretation on Overlay and Freeze, ordinary live BRK on Telnet (`monitor_debug_brk_session.cc:2192`, guarded by `!debug_owner.remote`) |
| Step Over of a ROM callee, and Step Out | **visible ROM** | **no** - a parked instruction walk with an 8192-step budget on Freeze only (`:2068`, `:2175`, `:2422`); breakpoint+Go on Overlay and Telnet |
| Continue To Cursor launched from | **visible ROM** | **no** - an extra priming step on Overlay and Freeze only (`:2673`) |
| Continue with breakpoints armed | any | **yes** - all three reach the same call |
| **Continue with no breakpoint armed** | any | **no** - three distinct paths: Freeze can defer the hand-back out of the monitor entirely (`machine_monitor_debug_impl.inc:866`), Freeze and Overlay otherwise fuse Continue with closing the monitor (`:887`), Telnet stays open and re-snapshots (`:910`) |
| ROM patch write and store selection | visible ROM | **yes** - address and CPU port only |
| The read of the byte a ROM patch displaces, and the install verify | visible ROM | **no** - both gated on `machine_is_frozen()` (`monitor_debug_u64.cc:215`, `monitor_debug_brk_session.cc:637`) |
| any step that runs the live CPU | any | **partly** - Freeze adds unfreeze/refreeze plus a staged NMI plus a chrome restore around every run |

Reading that table gives the transport requirement directly:

- **Visible ROM has to be run on all three transports.** Each takes a different
  path for at least one of linear step, Step Over/Out, and Continue To Cursor,
  and Freeze additionally takes a different path for the patch-original read.
- **Every transport needs at least one Continue with an empty breakpoint
  table.** Every matrix cell performs exactly that: it clears the breakpoint it
  continued to, asserts the slot table is empty, and only then continues.
- **RAM and RAM-under-ROM stepping transfer**, so their depth coverage can be
  bought once, on whichever transport gives the strongest oracle.

That last point matters because the transports are not equally observant.
`RestDebugDriver.active_debug_readback_allowed()` returns `False`: on Overlay
and Freeze the harness will not read device memory while a Debug session is
active, because a REST read is not a proven oracle for a live target there. So
those lanes validate PC, SP, A, X, Y and flags from the on-screen footer and
from the two instruction oracles, but they do not prove the pushed return
address on the stack, and they do not confirm the fixture's sentinel writes.
Telnet does. Telnet is therefore where a memory mode's depth is worth buying,
and Overlay and Freeze are where a transport's own path is worth buying.

## 2. Where it has actually failed

26 cell failures are recorded across the 36 formal run ledgers. The matrix
stops at the first non-PASS cell by default and cells run in memory-mode-major
order, so cells 28-45 were reached in only four runs; the grid below is a lower
bound, and the boundary modes are understated rather than overstated.

| memory mode | telnet | freeze | overlay | total |
| --- | --- | --- | --- | --- |
| ram | 2 | 2 | 1 | 5 |
| ram-under-rom | 0 | 0 | 1 | 1 |
| rom | 5 (all BLOCKED) | 0 | 2 | 7 |
| ram-rom-ram | 0 | 1 | 5 | 6 |
| ram-rur-rom-ram | 0 | 1 | 6 | 7 |
| **total** | **7** | **4** | **15** | **26** |

Three signatures account for almost all of it:

1. **Overlay crossed with a boundary mode, failing at debug entry** - 11 hits,
   as a `goto $C000` or `goto $E002` timeout. One run failed all six
   boundary-Overlay cells plus both `rom/overlay` late repetitions in a single
   sweep.
2. **A `step_over` fixture round-trip mismatch at `$C1F1`** - 6 hits, all in one
   run, all in boundary modes, 4 Overlay and 2 Freeze. A memory-write path
   defect specific to bank-switching fixtures.
3. **`rom/telnet` repetition 1 BLOCKED** - 5 hits, all with the same message:
   the KERNAL at `$E000` was not the canonical `STA $56 / JSR $BC0F` path. This
   is not a debugger defect in that run. It is contamination: an earlier
   suite left the volatile KERNAL image corrupt, and the ROM cell's precondition
   check is the first thing downstream to notice. `AGENTS.md` records the same
   thing happening across suites - `machine-code-monitor-debug` passed 89/89
   while leaving `$E000` corrupt, and it surfaced half an hour later as a
   blocked matrix cell.

The wider corpus of 189 artifact ledgers - which includes deliberately-red
repro runs, so it is a map of where bugs have lived rather than a flake rate -
puts 96 of its failures on visible ROM (56 Freeze, 33 Telnet, 7 Overlay), 33 on
plain RAM and 13 on RAM-under-ROM.

Two lane-level rates from the prose records: `freeze:rom` was 1 in 4
intermittent while `overlay:rom` was 0 in 4 in the same sweep, and during the
B19 investigation `overlay:rom` failed roughly 85% of the time on the first run
from a fresh deploy.

## 3. What repetition buys

The matrix builds the same fixture from the same bytes for every repetition -
`build_fixture` takes no seed, and the per-cell `program_seed` is recorded in
the ledger but never used to vary anything. A second repetition is therefore an
identical sequence run against a machine that has already been through it once.

That turns out to be exactly the state the intermittent failures need. Of the
failures the ledgers recorded that were reachable at all, roughly half appeared
only on repetition 2 or 3 with repetition 1 of the same cell passing in the
same run:

| run | cell | repetition | signature |
| --- | --- | --- | --- |
| `20260807T160113Z` | ram-rom-ram / freeze | 2 | round-trip mismatch `$C1F1` |
| `20260807T160113Z` | ram-rur-rom-ram / freeze | 2 | round-trip mismatch `$C1F1` |
| `20260807T172216Z` | ram / overlay | 2 | goto `$C000` timeout |
| `20260807T202527Z` | ram-under-rom / overlay | 2 | goto `$E000` timeout |
| `20260807T210930Z` | rom / overlay | 2 and 3 | goto `$E002` timeout |
| `20260807T223348Z` | ram-rur-rom-ram / overlay | 3 | goto `$C000` timeout |

Every late-repetition failure is a debug-entry timeout or a round-trip
mismatch, which is to say genuinely intermittent behaviour rather than a
deterministic bug that the first repetition happened to miss.

So repetitions are not dropped. They are spent where a repeat-only failure has
actually been recorded, and only on Overlay and Freeze, which cost a third of a
Telnet lane. Telnet lanes run once.

Repetition is not what covers repeated arm and disarm cycles. The straight-call
block does that inside a single cell: 32 consecutive `JSR`s to the same helper,
stepped with Step Over, with the stack pointer returning to the same value each
time. It drives 32 arm, park, resume and disarm cycles from an identical state,
with the expected PC, SP and A exact at every position. That is the detector for
a leaked breakpoint slot or a park and resume that drifts the stack, and running
the cell again does not improve it.

## 4. What each lane costs

Reconstructed from `cell.log` phase timestamps in the surviving artifact
directories. Mean duration of a passing cell, in minutes:

| memory mode | telnet | freeze | overlay |
| --- | --- | --- | --- |
| ram | 5.61 | 1.64 | 1.65 |
| ram-under-rom | 5.67 | 1.81 | 1.73 |
| rom | 5.11 | 1.38 | 1.38 |

Telnet is three to four times slower than either local UI in every memory mode,
and the difference is almost entirely one phase:

| phase | ram / telnet | ram / freeze |
| --- | --- | --- |
| enter debug | 46 s | 22 s |
| 32-level Step Into | 54 s | 10 s |
| **100-instruction dual-oracle trace** | **160 s** | **30 s** |
| Continue to breakpoint | 26 s | 4 s |
| 32 straight calls | 48 s | 12 s |

Telnet pays a screen round trip per keystroke. The `rom / telnet` trace is
worse still, at 238 s.

This is why the plan buys depth on exactly two Telnet lanes and buys transport
coverage on the cheap ones, and it is why running every memory mode on Telnet -
which would be the obvious way to use the strongest oracle - was rejected.

## 5. The selection

14 cell runs from 9 definitions, 11 of the 21 semantic groups, and the closing
random-program run. Every memory mode is represented; no mode is deferred.

### Matrix cells, U64

All five memory modes are run. The three regions the debugger works in - plain
RAM, RAM under ROM, visible ROM - and the two ways a developer crosses between
them each cost real effort to get working, and none is inferable from another's
result. What the selection cuts is how many transports a mode is repeated on.

| cell | reps | what only this lane has |
| --- | --- | --- |
| `ram:overlay` | 1 | Plain RAM through the whole flow: Step Over, the 32-level chain, Step Out, both Continue variants, Reset, slot hygiene, and the 32-call straight-call run. The closing random-program run drives RAM stepping and JSR nesting much harder, but it performs none of Step Out, either Continue variant, Reset or slot hygiene, so it does not stand in for this cell. |
| `rom:telnet` | 1 | Visible-ROM linear steps on the live BRK path and Continue To Cursor from ROM without the priming step, both guarded by `!debug_owner.remote`. The only lane that reads the pushed return address off the stack rather than inferring it. |
| `rom:overlay` | 2 | Visible-ROM linear steps through the RAM trampoline combined with breakpoint+Go for Step Over and Step Out - a pairing neither other transport produces. Recorded failures are on repetitions 2 and 3. |
| `rom:freeze` | 2 | The parked instruction walk for Step Over and Step Out of a ROM callee, the two frozen-aperture patch reads, the staged NMI, and the unfreeze/refreeze around every run. Measured 1 in 4 intermittent. |
| `ram-under-rom:telnet` | 1 | The bank-switch-then-execute family, carrying the 32-level Step Into chain, the 32-call straight-call run, and the 100-instruction dual-oracle trace with stack readback. |
| `ram-under-rom:freeze` | 2 | Continue with no breakpoint while KERNAL is banked out, taken through the Freeze hand-back, plus the frozen patch-original read on a banked-out aperture. |
| `ram-rom-ram:overlay` | 1 | The commonest real debugging shape: a RAM program calls a ROM routine, stepped into and back out of, with the banking untouched throughout. It reaches the ROM leg from a live RAM context on the first crossing rather than after two bank switches. |
| `ram-rur-rom-ram:overlay` | 2 | Twelve boundary crossings in one session - RAM into RAM-under-ROM and back, then into visible ROM and back, twice. The worst intersection in the recorded history. |
| `ram-rur-rom-ram:freeze` | 2 | The same crossings with the freezer owning the machine, so each crossing also crosses an unfreeze/refreeze pair. |

### Semantic groups, Telnet

`monitor_debug_test.py` is pinned to Telnet deliberately: its liveness oracles
read the C64's own screen and jiffy clock while the machine is meant to be
running, and an Overlay or Freeze UI holding the machine invalidates all of
them.

| group | what only this group has | defect it detects |
| --- | --- | --- |
| `exit-liveness-reentry` | Leaving Debug and closing the monitor with no reset. Every matrix cell ends in a Reset, so this is the only natural exit exercised anywhere. | The stepper runs with `I=1`, and that mask leaking into the hand-back leaves BASIC with a dead cursor, keyboard and jiffy. A reset clears it, so only a natural exit can see it (`24aebf99`). |
| `debug` | 27 cheap UI-contract checks: which key toggles a breakpoint, which key falls through to Range mode, the popup chord, the refusal of an 11th breakpoint without evicting the other ten, the footer layout, the reset chord. | A remapped Debug key that the harness drivers still send under the old binding (`4e7aedbf`, `9dd32e8e`). |
| `refusal` | Over on BRK, RTS and RTI with no captured context; Out outside a traced subroutine saying NOT IN SUBROUTINE rather than PATCH FAILED; an undocumented NOP decoded but not stepped; RTI restoring the stacked PC and flags. | An undocumented opcode executed as the documented instruction sharing its bit pattern (`d66c1214`). |
| `page-cross` | Taken and not-taken branches across a page, and `JMP ($xxFF)` following the real page-wrap target. | A page-wrap target the step prediction resolves to the wrong address (`7091f16e`). |
| `rom-breakpoints` | Set, hit, remove and then step a breakpoint in the BASIC and KERNAL stores, the operation that writes into the volatile FPGA ROM image and has to put it back. | A ROM-image patch that is not restored on removal (`e298ab3f`, `d4434c19`, `8ff6a405`, `0ed125a8`). |
| `banked-breakpoints` | A KERNAL-store and a RAM-under-KERNAL breakpoint at the same `$E000` with the program banked out, and the cleanup that restores both stores. | A displaced byte saved from the wrong store and written back into the other, as described in section 6 (`dd535e9a`, `08c6989e`). |
| `repeat-redebug` | Cancelling and re-entering Debug repeatedly against a running loop, in RAM and with KERNAL banked out. The ownership and reopen-state path, not the stepping path. | A leaked `debug_owner` that leaves the monitor stuck showing `Dbg` through both monitor teardown and a machine reset (`ab86bd8f`, `35f90fe6`, `fc9826db`, `8d7ae9d4`). |
| `banked-continue-no-breakpoints` | Continue with an empty table at `$01=$00`, `$35` and `$37`. The matrix reaches `$35` and `$37`; CPU0, all RAM and no I/O, is reached nowhere else. It also proves a Continue with breakpoints leaves the live backing store intact. | Continue with no breakpoint falling to an NMI trampoline that has no handler when KERNAL is banked out (`2af4728d`, `c6773f10`). |
| `side-effect-step` | Step Over must execute what it steps over: the store lands, the subroutine's side effect happens, the skipped branch's store does not. The dual-oracle traces compare CPU state, not every write. | A Step Over that reports the right CPU state without performing the memory writes. |
| `breakpoint-reentry` | Continue issued from the breakpoint the CPU is already stopped on: step off once and re-arm, not trap on itself forever. | A Continue that re-traps on the breakpoint it started from (`8ff6a405`). |
| `brk-orchestrator` | The plain-RAM smoke over Telnet, end to end in about half a minute: load a program, Continue to a breakpoint with known registers, Step Over a NOP, Step Into the JSR, Step Out, and prove the cleanup put the user's bytes back under the breakpoint. | A break in the ordinary RAM debugging path, caught in 30 seconds rather than in a full cell. |

`nested-out` and `step-out-target` are not in this list because the matrix
preflight already runs them, so the gate gets them for free.

### The closing random-program run

`run_opcode_volume` shells out to `monitor_debug_stress.py`, which generates
random programs and steps them through the Overlay UI against the independent
6510 interpreter. It is the only differential
oracle in the tree that generates its own programs, and it is where the
undocumented-opcode and page-wrap defects were found.

The gate runs it with 6 random programs rather than the matrix's 12. Measured,
12 programs land 2592 verified instructions in 15-22 minutes against a
requirement of 1000; 6 land roughly 1300 in about half that. **The 1000
instruction requirement is not lowered** - what is given up is the headroom and
the second half of the random program space. The knob is the matrix's new
`--opcode-iterations`; `--opcode-run`, the requirement, stays at its default.

### U2+L in a C64 Ultimate host

A split session supports one debugger lane. Visible-ROM stepping refuses with
`BRK $E000 IN ROM BLOCKS DEBUG`, RAM-under-ROM entry is not demonstrated, and
the cartridge has no `Interface Type` setting at all - its only UI is the
freeze overlay - so `overlay` and `freeze` drive the same firmware through the
same keys and a second lane would be a copy of the first. Every recorded u2
matrix ledger holds a single row.

The repetition count is three, not two. The matrix fixture seed is a function
of the repetition index, so each repetition runs a fixed program. Repetition 3's
program is the one that reproduced the torn debug-footer read (the register row
copied by `menu_screen` while it was half redrawn), twice in two runs, with
repetitions 1 and 2 passing both times. Two repetitions would not reach it.

The split plan is therefore `ram:overlay` three times, the seven semantic groups
whose checks are reachable, and the two focused scopes that exist only for a
split session and that no cell reaches, because the matrix driver skips monitor
bank selection on a U2:

- `--focus banking`: five CPU-port states resolved to their visible sources,
  including the CPU0/CPU4 pair that expose the same map, which is what proves
  CHAREN itself was captured rather than inferred from the ROM signatures.
- `--focus entry-footer`: six CPU-port states x four VIC banks, asserted on the
  monitor's **first** frame. A footer that is only correct after a step is a
  footer reporting the debugger's banking instead of the program's.

Two preconditions have to hold on the host or every assertion fails in a way
that looks like a firmware defect: `Cartridge Preference` must be `External` on
the **host** (a mains power cycle resets it to `Auto`, and the setting only
takes effect after the computer reboots), and the C64 Ultimate's own menu must
be closed - with any host UI up, injected keys are accepted with HTTP 200 and
consumed by the host's menu.

### What was cut because it duplicated something already in the gate

Every item here was considered, found to assert a property something else in
the gate already asserts, and dropped for that reason rather than for cost.

| dropped | already asserted by |
| --- | --- |
| `nested-out` and `step-out-target` | The matrix preflight runs both groups itself. `monitor_harness_test.py` asserts the selected list and the preflight list are disjoint, so this cannot silently come back. |
| `--focus alerts` | `machine-code-monitor-harness` already runs `validate_debug_alerts()` and the manual-text contract, deterministically and with no device. |
| `flags` | Every cell step compares the full status register against both oracles. |
| `jsr-runcursor-rts` | Every cell step compares SP against both oracles, and the JSR/RTS shapes it uses appear in the 32-level chain and the straight-call run. |
| `deep-trace` | Its D/T/G/O walk across RAM, KERNAL and BASIC is the `ram-rur-rom-ram` traversal, which crosses more regions and checks each crossing. |
| `rom-single-step` | The `rom` cells step the live ROM path linearly on all three transports and trace 100 instructions of it. |
| `kernal-basic-breakpoint` | Its BASIC-store breakpoint duplicates `rom-breakpoints`; its `$E002` to `$BC0F` continue is the `rom` cells' Continue. |
| `cleanup-exit`, `ram-edit`, `edit-visibility` | Monitor-editor and exit behaviour that `machine-code-monitor` covers, and that suite already runs in the ordinary sweep. |
| Plain RAM and `ram-rom-ram` on transports other than Overlay | Section 1 shows RAM stepping and control-flow steps take the same path on all three transports. Both modes are run, on Overlay. |
| 31 of the 45 matrix cell runs | Section 1 shows their result is predicted by a lane that is run. |

### Residual overlap

The debug suite's finest selector is the group, so a group cannot be taken
apart. Three of the ten selected groups carry a check that a selected cell also
covers:

| group | overlapping check | why the group is kept anyway |
| --- | --- | --- |
| `rom-breakpoints` | The KERNAL-store half overlaps the `rom` cells' Continue-to-breakpoint. | The BASIC-store half is the only breakpoint armed in `$A000-$BFFF` anywhere in the gate. |
| `banked-breakpoints` | Arming one store at `$E000` overlaps the `ram-under-rom` cells. | Arming a KERNAL-store and a RAM-under-KERNAL breakpoint at one address at once, and restoring both, is unique. |
| `banked-continue-no-breakpoints` | The `$01=$35` and `$01=$37` Continues overlap the `ram-under-rom` and `rom` cells. | `$01=$00` - all RAM, no I/O - is reached nowhere else, and neither is a Continue that has breakpoints armed, proving the live backing store survived. |

That is about 4 duplicated checks out of roughly 71, near two minutes of the
60. Removing them would mean either forking a group, which is the duplication
this suite exists to avoid, or losing the unique check beside them.

One further overlap exists only on a split session: the sole supported lane is
`ram` on the local UI, and the random-program run drives the same lane. The
cell runs a fixed fixture through the full flow - Step Out, Continue To Cursor,
Continue To Breakpoint, Reset, slot hygiene - which the random-program run does
not perform; the random-program run steps generated code, which the cell does
not. They overlap in the lane and not in what they assert.

## 6. The one check this gate adds

Everything above is a selection. One thing is new, because nothing in either
suite asserted it.

When the debugger arms a breakpoint against the BASIC or KERNAL store it writes
a BRK into the volatile FPGA ROM image and records the byte it displaced so
removal can put it back. That read went through `peek_cpu(addr, cpu_port)`, and
`cpu_port` cannot re-bank the aperture on this hardware: RAM `$00/$01` on a U64
is a DMA-only mirror of the 6510's port, so the aperture answers from whatever
the running program has mapped. Arm a breakpoint in the KERNAL store while
the program runs at `$01=$35` and the debugger saves the RAM byte hiding under
`$E000`, then writes that byte into the KERNAL image on removal. It was measured
on hardware: `$E000` left holding `$EE`, the first byte of the scenario's
`INC $D020` payload, where the KERNAL's `STA $56` puts `$85`. It survives a
machine reset. Only a firmware restart reloads the images.

`read_patch_original_byte()` is the read that has to answer from the store the
breakpoint is in. It is separate from `read_patch_byte()`, which feeds step
prediction and disassembly and has to answer from the live aperture.

Nothing asserts the result. The matrix's `rom` cell checks the KERNAL head as a
precondition and blocks when it is wrong, which names the run that inherited the
damage rather than the run that caused it.

`RomImageFence` reads the `$E000` and `$A000` heads after a reset before the
gate touches the debugger, and again after it has finished. It compares against
this run's own snapshot rather than a hard-coded constant, because the bench
device runs JiffyDOS and a stock KERNAL head would fail on a healthy machine.
It skips itself on a split session, which has no writable ROM image, and it
skips rather than passes if either head reads as a single repeated byte, which
means RAM or an unmapped aperture rather than a ROM.

There is also no host test for `read_patch_original_byte`,
`known_live_cpu_port` or `live_aperture_serves`. That gap is noted here and not
closed by this work.

## 7. What this gate does not cover

Each of these is in the full matrix or the full debug suite.

| not covered | why it is safe to defer |
| --- | --- |
| Plain RAM and `ram-rom-ram` on Telnet and Freeze | Both modes are run, on Overlay. RAM stepping and control-flow steps take the same path on all three transports, plain-RAM debugging over Telnet is covered end to end by `brk-orchestrator`, and RAM stepping harder still by the random-program run. |
| `ram-under-rom` on Overlay | The Overlay hand-back path is exercised by `rom:overlay` and by both boundary-mode Overlay repetitions. Only the banked-out Continue would be new, and that leg is inside the shared `go()`. |
| Telnet on either boundary mode | No boundary-mode Telnet cell has ever failed, and a Telnet cell costs three to four times an Overlay or Freeze one. |
| A third repetition anywhere | Repetition 3 has produced one recorded failure that repetition 2 did not. |
| 10 of the 21 semantic groups | `kernal-basic-breakpoint`, `deep-trace`, `jsr-runcursor-rts`, `flags`, `cleanup-exit`, `ram-edit`, `edit-visibility`, `rom-single-step`, `nested-out`, `step-out-target`. The last two run anyway inside the matrix preflight; the rest assert a property some matrix cell asserts against two oracles. |
| The random-program run's headroom | 6 programs rather than 12, as described above. |
| The monitor itself | `machine-code-monitor` is registered `auto` and already runs in the ordinary sweep. The recommended pre-merge command is `./run-tests <target> --manual -s machine-code-monitor -s machine-code-monitor-regression`. |

Two known-open items are inside the gate and will fail it if they recur, which
is intended, but they are not this gate's to fix:

- `exit-liveness-reentry` check `[03]`, the visible-KERNAL step, fails about 1
  in 6. The footer is byte-identical every time, so the CPU trapped on a stray
  `$00` in uninitialised RAM rather than on the step trampoline. Adding roughly
  0.8 s of polling between the breakpoint clear and the step makes it pass 6 of
  6. Do not "fix" it by adding a reset to that check: that deletes the
  live-machine coverage the check exists for.
- On a U2+L, leaving the monitor after a debug step parks the CPU leaves the
  6510 DMA-held. All four exit routes fail identically, and only a U2 menu
  toggle clears it.

## 8. Confounds that must not be read as debugger defects

Each of these has been filed as a firmware defect at least once and was none.
A gate read without them in mind manufactures failures.

| confound | effect |
| --- | --- |
| The Telnet remote screen is 60x24, not 40x25 | The backend rendered into a 40-column emulator, cutting off the columns holding the `Dbg` flag at `width-8`. `_ensure_no_debug()` returns early when `Dbg` is absent, which at 40 columns was always, so the suite never left Debug anywhere and the machine looked dead. 26 fake failures; 88/88 after. |
| A full-budget poll nested inside a retry loop that owns its own budget | The loop runs once, value waits sample the footer once, and every check reports the previous check's PC. Indistinguishable from slow firmware. Seven failures were about to be filed. Pass `timeout=0.0` for a single read. |
| Python `AttributeError`/`NameError`/`TypeError` classified as `VALID_DEBUGGER_DEFECT` | Two harness bugs killed every lane and were reported as debugger defects. 2/15 aborted became 15/15 PASS. |
| Telnet load degradation within one run | Late groups intermittently miss breakpoints or break the pipe; every group passes on fresh firmware. A redeploy is recovery, never a pass. |
| `$02A7 == 1` read as "the installed BRK fired" | It means some BRK fired, not the installed one. A runaway that hits a stray `$00` sets it too. Only "did the step reach the expected PC" is trustworthy. |
| Instrumented builds | Extra DMA peeks between BRK commit and CPU release deterministically change which failure mode appears. Never use an instrumented build as the pass/fail oracle. |
| A reset-and-retry loop around a debugger launch | Turns an intermittent defect into a green result. `monitor_harness_test.py` enforces this structurally, over the AST, and the matrix declares prohibited counters. |
| Endurance wedges | On a U64, REST dies while ICMP still answers. On a C64 Ultimate, `menu_button` returns 200 and the menu never opens while every health check passes; only mains power clears it, and it cascades into several unrelated-looking suite failures. |
| A stale ELF | The device reverts to flashed firmware on reboot and the on-disk ELF mtime lies. Verify the firmware identity before a campaign; the matrix stamps it at preflight and again at the end and fails the run if it changed. |

## 9. Projected cost

Derived from the per-lane measurements in section 4, plus a preflight measured
at 2.3 minutes from the runs that died in it.

| part | U64 |
| --- | --- |
| matrix preflight (interpreter self-test, two debug groups, freeze re-entry guard, local-UI soak, VICE oracle check) | ~2 min |
| 14 cell runs | ~31 min |
| random-program run, 6 programs | ~8 min |
| 11 semantic groups | ~19 min |
| ROM image fence, twice | ~1 min |
| **total** | **~60 min** |

Against 74-82 minutes for a clean full matrix plus 28-50 for the full debug
suite: a little over a third of the hardware time, and one command.

On a `u2@c64u` split session the cell count drops to 2 but every keystroke
crosses the expansion bus, the random-program run is about 2.6 times slower per
program, and the two focused scopes are added. Expect **45-60 minutes**. That
figure is a projection: no 45-cell matrix run has ever completed on a split
session, so there is no measured baseline to scale from.

## 10. When to run the full matrix instead

- Before a release, rather than before a merge.
- After any change to `monitor_debug_brk_session.cc`, `machine_monitor.cc` or a
  backend's patch or memory paths, where the transport-transfer argument in
  section 1 is exactly what the change may have invalidated.
- After a merge from `upstream/test-merge`. A merge that resolves a conflict in
  favour of one side deletes the other side's debugger behaviour without
  failing to build, so treat an upstream merge as a regression event.
- When a lane this gate defers is the lane under suspicion.
