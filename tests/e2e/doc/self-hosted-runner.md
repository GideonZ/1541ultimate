# The E2E gate's runner

[`.github/workflows/e2e.yml`](../../../.github/workflows/e2e.yml) targets
`runs-on: [self-hosted, e2e]`. No such runner exists yet, so the workflow is
valid and has never run; everything it calls has so far been run by hand. This
document is what somebody standing up that machine needs, and nothing else.

The `e2e` label is the whole selection mechanism. `build.yml` uses the bare
`self-hosted` label, so a job without the second label lands on the build
machine, which is not on the device LAN and has no devices attached.

## What the machine has to be

One host on the same LAN as the devices, reachable from them and able to reach
them. It needs:

| Requirement | Why |
| --- | --- |
| A GitHub Actions runner registered with the `self-hosted` and `e2e` labels | How the workflow finds it |
| Python 3.10 or later, and `pip install -r tests/requirements.txt` | The suites and the report generator |
| Network reach to every device the gate names | REST (80), FTP (21), Telnet (23), the DMA control port (64), and ICMP |
| UDP 5514 open inbound | Where `--syslog` collects the devices' own log |
| `ffmpeg` and `ffprobe` with the `libx264rgb` encoder | `--record`, which the workflow passes by default; a build without the encoder is refused at startup |
| Exclusive use of the devices | The gate drives the menu, resets the machine and mounts images |

The devices must be otherwise idle for the length of a run. The workflow's
`concurrency: e2e-gate` group keeps two gate runs apart, but nothing stops a
person or another agent driving the same device by hand, and a run that meets
one reports failures that are not the firmware's.

Set `secrets.U64_PASS` if the devices have a REST password, and
`vars.E2E_RECOVER_COMMAND` to whatever brings a wedged device back on this
machine. Both are read from the environment rather than a command line: a
recorded command line is a secret-bearing string, and the artefacts leave the
machine.

## Deploying the firmware under test

The gate runs whatever firmware the devices are already running. Nothing in
`run-tests` deploys, and a green run against a stale image proves nothing about
the commit that triggered it, so a deploy step belongs before the gate step.

Two routes, and they are not equivalent.

### JTAG, which is the one to use

`nios2-download` writes the built ELF into the running device over a USB
Blaster and starts it. Nothing is written to flash, so the worst case is a
device that has to be power cycled back to its flashed image. It needs no
device cooperation at all: it works on a device whose UI has wedged, which is
exactly when a deploy is needed most.

`tooling/build_and_deploy_u64.sh` here does that for an Ultimate 64 in about
30 seconds. It is a local script rather than a tracked file, because the
toolchain path and the cable are properties of one bench, so what it does
matters more than where it is. Five steps, none of them repository-specific:

1. Refuse unless `target/u64/nios2/ultimate/result/ultimate.elf` exists and is
   non-empty, so a failed build cannot be deployed as a stale one.
2. Refuse unless `jtagconfig` and `nios2-download` are present under the
   Quartus install (`INTEL_FPGA_ROOT`, default `/home/chris/altera_lite/18.1`).
3. Put the Quartus and Nios II tool directories on `PATH` and export
   `QUARTUS_ROOTDIR` and `QSYS_ROOTDIR`.
4. Run `jtagconfig` and refuse if it lists no cable, so "no hardware" is a
   message rather than a confusing download error.
5. Run `nios2-download -g <elf>`, which pauses the Nios, writes, verifies and
   starts it.

A runner that wants this needs the same five steps and its own paths.

The cost is physical. Each device needs a JTAG cable to the runner, and the
runner needs the vendor toolchain installed (Quartus and the Nios II tools for
the Altera devices).

Because a JTAG download does not touch flash, a device that loses power comes
back on its flashed image. A gate that assumes otherwise reads a mains blip as
a firmware regression, so a deploy step should run before every gate run rather
than once when the machine was set up.

### The menu route, which is a last resort

Without JTAG, firmware is installed the way a user installs it: upload the
update image over FTP, then drive the device's own updater through its menu
with injected keystrokes.

This writes flash. An image that is wrong for the device, or a run interrupted
partway through, leaves a device that does not boot and that no amount of
network access can recover: the fix is a JTAG cable or a return to the vendor.
It also depends on the device's UI answering injected keys, which is precisely
what fails when the device is in the state that needed a deploy.

`tools/api/u2_flash.py` here automates it for a U2+L. It is also a local script
rather than a tracked file. What it does is worth knowing whether or not a
runner reuses it, because every step exists to make a wrong flash impossible
rather than merely unlikely:

- It reads the screen from the cartridge and sends every keystroke to the
  computer. The cartridge serves its own `machine:menu_screen`, which no other
  device can see, and answers `machine:input` with HTTP 501, because that
  endpoint is compiled only for Ultimate 64 hardware. The keyboard matrix is a
  real signal that reaches the cartridge over the expansion port, so keys go to
  the computer and are read back off the cartridge's own screen.
- It uploads the artifact over FTP to a path naming the branch, the date and
  the commit, verifies the upload, and refuses to overwrite an artifact already
  there rather than flashing something it did not just upload.
- It navigates to the file and stops. `--confirm-flash` is what presses `Run
  Update`; without it the tool proves the navigation and backs out.
- During the flash the cartridge's own REST goes away, so it follows progress
  by reading the C64's screen RAM through the computer, and treats
  `PLEASE TURN OFF YOUR MACHINE` on three consecutive reads as completion.
- The U2+L updater cannot restart itself, so completing the update means
  power-cycling the computer that powers the cartridge port.
  `--confirm-power-cycle` is what allows that, through the computer's own
  `Power & Reset` menu.
- Every failure path aborts with the state it reached and what it observed,
  rather than continuing on a guess. An abort before `Run Update` has changed
  nothing.

Prefer JTAG. Where the menu route is the only one available, use a tool that
refuses rather than guesses.

## Collecting the devices' own log

`--syslog` collects what the devices send. It does not configure anything: each
device's `Network Settings` / `Log to Syslog Server` has to point at the
runner's address, and that setting takes effect at the device's next boot. The
runner reads the setting at both ends of a run and warns if it is wrong or
changed, but corrects it at neither end.

The runner also reads the port out of that setting before it binds anything,
and binds one socket per port the devices name, plus its own `--syslog-port`
default. A datagram is then attributed by the socket that received it wherever
that is possible: a port exactly one machine sends to identifies that machine
whatever address the datagram came from.

That matters because a datagram carries no identification but its source
address, and a machine's source address is not always the address its name
resolves to. An Ultimate 64 with Ethernet and WiFi both up answers REST on one
and can send its log from the other.

Give each device a port of its own and the WiFi case is attributed correctly:

```
c64u    Log to Syslog Server = 192.0.2.10:5515
u64     Log to Syslog Server = 192.0.2.10:5516
u2      Log to Syslog Server = 192.0.2.10:5517
```

Setting it is a provisioning step, not something a run does. `Syslog::init` is
called once from `ultimate_main`, so a value written during a run does nothing
until the machine boots again, and `PUT /v1/machine:reboot` does not reach that
far: it reboots the C64 and leaves the Ultimate firmware running. Write the
value, save that one store with
`PUT /v1/configs/Network Settings:save_to_flash`, and power-cycle the machine.
The C64 Ultimate's own F1 menu has Power & Reset then Power Cycle, which
`tools/api/u2_flash.py` drives, and which restarts its cartridge port with it.

Save one store rather than the whole tree. In safe mode every store reads as
its defaults, and `PUT /v1/configs:save_to_flash` would then write those
defaults over the real values. Before saving anything, read the store and check
that at least one item differs from its default, which a device loading
defaults cannot show.

None of this is required. A bench where every device sends to one port behaves
exactly as it did before: the port identifies nothing, so attribution falls
back to the source address, and an address no target claims still goes to
`syslog-unknown-sender.txt` rather than being guessed at. What the ports buy is
attribution that survives a device logging from an address the run did not
expect.

The source address is recorded either way. The report shows each target's
expected addresses, the ports it was collected on, the addresses its lines
actually arrived from and whether the port or the address attributed them, so a
device that moved interfaces is visible rather than absorbed by the port match.

`U64_LOG_ADDRESSES` remains as an escape hatch for a machine sharing a port
whose log arrives from an address its name does not resolve to. It attaches
further addresses to a named machine for the length of the run:

```sh
U64_LOG_ADDRESSES="u64=192.0.2.71" ./run-tests -o runs/ --syslog u64
```

Lines from an address no target claims are kept either way, in
`syslog-unknown-sender.txt` with the address that sent them, which is what makes
the omission visible rather than silent. The report lists every such sender with
its line count, and lists each target's expected and observed sender addresses
beside it, so the two tables together say whether a device sent from somewhere
unexpected or another machine on the LAN is logging to the same collector.

Not every device sends a log. A device whose firmware line has no syslog
support produces no `syslog.txt`, and the run records that it started a
collector and received nothing, which is a different fact from a collector that
never started.

## What a run leaves behind

The workflow runs the gate with `-o "$RUNNER_TEMP/e2e"`, generates
`index.md` from that tree with `tools/e2e_report.py`, and uploads it. The
report and the JSONL go in one artifact and the recordings in another, because
the two have different sizes and different useful lifetimes.

It also passes `--syslog` and `--record`. Both are `workflow_dispatch` boolean
inputs defaulting to true, and the scheduled run passes both whatever the inputs
say: it is the run nobody is watching, so its failure has to be diagnosable from
the artefacts alone. That costs the runner a UDP port and the devices two
streams for the length of the run, which is the trade the requirements above are
sized for.

Every step after the gate runs `if: always()`. A run that was cancelled or that
hit the job timeout is the run whose evidence is worth most, and the generator
renders a half-written tree, so there is nothing to gain from only uploading
after a pass.

The gate step itself is `continue-on-error: true`, and a later step fails the
job on the gate's own `outcome`. That ordering is what lets the report exist
for a failing run.

## Reading the result

The exit status carries the outcome, so nothing has to parse the console:

| Status | Meaning |
| --- | --- |
| 0 | Every suite passed and no device needed recovering |
| 1 | A suite failed |
| 2 | The command line was wrong |
| 3 | Every suite passed, but a device had to be recovered |
| 4 | A device could not be made healthy, and the run was abandoned |

3 is not 0 on purpose: a device that had to be brought back mid-run is not the
same result as one that did not.
