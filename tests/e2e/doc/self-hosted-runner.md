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
| `ffmpeg` and `ffprobe` with the `libx264rgb` encoder | `--record` only; a build without it is refused at startup |
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
device that has to be power cycled back to its flashed image.
[`tooling/build_and_deploy_u64.sh`](../../../tooling/build_and_deploy_u64.sh)
does this for an Ultimate 64 in about 30 seconds and needs no device
cooperation at all: it works on a device whose UI has wedged, which is exactly
when a deploy is needed most.

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
[`tools/api/u2_flash.py`](../../../tools/api/u2_flash.py) automates that for a
U2+L, including the power cycle its updater cannot perform for itself.

This writes flash. An image that is wrong for the device, or a run interrupted
partway through, leaves a device that does not boot and that no amount of
network access can recover: the fix is a JTAG cable or a return to the vendor.
It also depends on the device's UI answering injected keys, which is precisely
what fails when the device is in the state that needed a deploy.

Prefer JTAG. Where the menu route is the only one available, keep to a tool
that refuses rather than guesses: `u2_flash.py` verifies the uploaded image
before it navigates, requires `--confirm-flash` to press the final key, and
aborts safely on anything it did not expect rather than continuing.

## Collecting the devices' own log

`--syslog` collects what the devices send to UDP 5514 on the runner. It does
not configure anything: each device's `Network Settings` / `Log to Syslog
Server` has to point at the runner's address, and that setting takes effect at
the device's next boot. The runner reads the setting at both ends of a run and
warns if it changed, but corrects it at neither end.

A datagram is attributed to a device by its source address. A device with two
interfaces logs from whichever one its routing picked, which is not always the
address its name resolves to: the Ultimate 64 here answers REST on its Ethernet
address and sends its log from its WiFi address, and nothing on its REST
surface reports either. Attach the second address for the run:

```sh
U64_LOG_ADDRESSES="u64=192.0.2.71" ./run-tests -j runs/ --syslog u64
```

Without it those lines are still kept, in `syslog-unmapped.txt` with the
address that sent them, which is what makes the omission visible rather than
silent. A non-empty `syslog-unmapped.txt` is the symptom; this variable is the
fix.

Not every device sends a log. A device whose firmware line has no syslog
support produces no `syslog.txt`, and the run records that it started a
collector and received nothing, which is a different fact from a collector that
never started.

## What a run leaves behind

The workflow runs the gate with `-j "$RUNNER_TEMP/e2e"`, generates
`index.md` from that tree with `tools/e2e_report.py`, and uploads it. The
report and the JSONL go in one artifact and the recordings in another, because
the two have different sizes and different useful lifetimes.

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
