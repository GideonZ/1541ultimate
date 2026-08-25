# Bisecting the Doom C64U regression

Doom C64U streams level geometry, textures and sprites out of a 16 MB REU every
frame while the CPU runs at turbo speed, which makes it the most demanding
public exercise of the U64 bus there is. Two firmware releases broke it, and
this directory holds the harness that found where.

## What was found

Two separate regressions, on adjacent bitstream bumps. Both commits change
**only** `external/u64.sof` and `external/u64_mk2_artix.bit`, so neither is a
firmware source change.

| | first bad commit | last good | effect |
|---|---|---|---|
| Picture corruption | `c4be69a2` (2026-05-02) "Adds cartridge compatibility mode to U64/U64E2 FPGA builds (untested)" | `1e387255`, which carries the `f2a14e51` bitstream | the game runs at full speed but pixels in the 3D view are randomly wrong |
| Total failure | `77f3b381` (2026-05-11) "Updated bridge timing and REU in U64/U64E2" | `198353a8` | `mapOK` is 0, `frameCnt` never advances, the screen is garbled |

`198353a8` is the last commit that still *runs* the game; it already carries the
corruption, because its bitstream is `c4be69a2`'s.

Measured on an Ultimate 64 Elite I. Reference figures, PAL, at the level's
starting position with no input:

| bitstream | date | frame rate | frames that differed | different pixel sets |
|---|---|---|---|---|
| `87bd7700` (v3.14) | 2025-11-11 | 12.6 fps | none at all | 0 |
| every bitstream up to `d0d1934a` | to 2026-04-11 | 12.6 fps | 0 to 31% | 1 to 3 |
| **`bff2d1eb`** (`c4be69a2`) | 2026-05-02 | 12.6 fps | **100%** | **503** |
| `77cf7e40` (`77f3b381`) | 2026-05-11 | 0 fps, `mapOK=0` | engine never starts | |

On `c4be69a2` nearly every differing frame is wrong in its own way, up to 2130
pixels at a time, in bursts of four captured frames: the engine renders at about
12.6 fps into a 50 Hz output, so one rendered frame occupies about four captured
ones and every second rendered frame is corrupt. That is the roughly 8 Hz
flicker of a few pixels that a person sees.

`Bus Operation Mode = Compatibility` restores a correct picture on affected
firmware but drops the machine to about 1 MHz: 0.30 fps against 12.6. It is a
diagnostic, not a workaround.

### What this does and does not say about the FPGA sources

The bitstreams live in this repository as prebuilt binaries at
`external/u64.sof`, and they are built from `GideonZ/ultimate64`. **Do not map a
bitstream to a source commit by date.** Per the author, the two repositories are
not kept in sync, the submodule link is not maintained, and a checked-in
bitstream can be *older* than the source commits around it. An earlier version
of this document named specific `ultimate64` commits on that basis; that
attribution was wrong and has been removed.

What the sweep does establish is which **bitstream binary** misbehaves, and that
is what to hand to whoever can map it to sources:

| bitstream blob | introduced by | behaviour |
|---|---|---|
| `d0d1934a` | `f2a14e51` | last one that renders correctly |
| `bff2d1eb` | `c4be69a2` | renders, corrupted |
| `77cf7e40` | `77f3b381` | engine does not start |

Get a blob's identity with `git rev-parse --short <commit>:external/u64.sof`.
`core_version` and `fpga_version` in `/v1/info` are hand-bumped labels that
several distinct builds share, so they do not identify a bitstream.

Also per the author: the timing constraints for these designs are present and
every checked-in bitstream meets timing. An earlier version of this document
speculated that a change had pushed the design into timing marginality. There is
no evidence for that and it should not be repeated.

## How the picture is judged

The player never moves during a measurement, so **every frame of video should
look exactly the same**. The harness captures about a thousand frames and
compares each one against the first.

Some frames will still differ, even on a machine that is working perfectly, so
"any difference at all" cannot be the test. What separates a working machine
from a broken one is **whether the same pixels keep changing, or different ones
each time**:

- **A working machine changes the same pixels over and over.** If the game
  animates something, it is the same handful of pixels every time, so the
  picture only ever alternates between a small number of fixed images.
- **A broken machine changes different pixels every time.** When data coming
  back from the REU is wrong, whichever pixels happen to be affected are wrong
  *this* frame, and a different set is wrong the next.

So for every frame that differs, the harness records **which pixels** differed
(that list of pixel positions is what the code calls a "change mask") and then
counts **how many different such lists it saw**:

| what the numbers mean | working machine | broken machine |
|---|---|---|
| how many frames differed at all | anything from none to most of them | most of them |
| **how many *different* sets of pixels were involved** | a handful, because the same ones keep changing | hundreds, a new one nearly every frame |

Measured by sweeping every bitstream between v3.14 and v3.15 on an Ultimate 64
Elite, three launches each, about 1000 frames per launch:

| bitstream | frames that differed | different sets of pixels involved |
|---|---|---|
| v3.14 | none at all, in all three launches | 0 |
| every bitstream up to `f2a14e51` | 0 to 31% | 1 to 3 |
| **`c4be69a2`** | **100%** | **503** |

A build is called broken when it shows **at least 10 different sets of pixels**
and those account for **at least 30% of the frames that differed**. Both halves
are needed: a couple of different sets turn up on healthy machines, and "every
differing frame was unique" means nothing when only three frames differed.

`index.json` calls these `distinct_masks` (how many different sets of pixels)
and `deviating` (how many frames differed at all); their ratio is `ratio`.

Four measurement traps, all handled by the scripts here, each of which produced
a wrong answer before it was:

- **Frames must be assembled by header offset, not by concatenating packet
  payloads in arrival order.** A reordered packet then shifts the rest of the
  picture, and differently every time, which looks exactly like the defect being
  hunted. `streams.FrameAssembler` places each packet by its offset and returns
  only complete frames; do not write another assembler.
- **The video multicast group is shared between devices.** Another Ultimate
  streaming into the same group has its packets assembled into these frames.
  `streams.receive` reports whether the sender was this device.
- **A joystick in port 2 moves the player without anyone touching it.** The
  soak reads the player's position and reports whether it moved before the first
  differing frame, so such a run is voided rather than counted as a defect.
- **The defect can hide for a whole launch.** Rates vary between launches of one
  and the same bitstream, so several launches are required before a build is
  called clean.

`doom_soak.py` also proves the capture can show a change at all, by poking the
VIC border and requiring the picture to move. Without that, a frozen capture
would pass trivially. Writing the player's position does not work as a check:
the engine undoes an out-of-bounds position and renders from cached sine and
cosine values.

## Running it

### Prerequisites

- A USB Blaster on the JTAG header, and Quartus 18.1 (`quartus_pgm`,
  `nios2-download`) under `$INTEL_FPGA_ROOT`, default `~/altera_lite/18.1`.
- The Docker build image, `1541u-build:latest` or `my_docker_image`.
- A git worktree of this repository for the harness to check commits out into,
  which must not be the one you are working in:

```sh
git worktree add --detach ~/.cache/doom-bisect/wt v3.14
```

- `pip install numpy pillow`, and the device reachable by name (`u64`).

Environment variables, all optional: `DOOM_HOST` (device name, default `u64`),
`DOOM_REU` (path to `game.reu` on the device), `DOOM_BISECT_WORKTREE`,
`DOOM_BISECT_LOG`, `DOOM_BISECT_LOGDIR`, `BUILD_IMAGE`, `BUILD_TOOLS_ROOT`,
`INTEL_FPGA_ROOT`, and `JTAG_CABLE` (default `USB-Blaster [1-6]`; run
`jtagconfig` to see what this machine calls its cable).

### Install the game

```sh
tests/bisect/doom/install_assets.py --host u64 --dir /USB2/doom
```

The USB volume is named differently on different machines; check an FTP
listing if `/USB2` does not exist.

### Judge one commit

```sh
tests/bisect/doom/verdict.sh 77f3b381
```

Deploys that commit's bitstream and application over JTAG, then launches the
game three times and prints `FINAL: GOOD`, `BAD`, `BROKEN` or `SETUP_FAILED`.

### Sweep a range and keep the evidence

```sh
tests/bisect/doom/bisect_run.py --range v3.14..v3.15 --launches 3
```

This is the one to reach for. It measures **every** bitstream in the range,
records each with sound, and writes a run directory laid out the way
`run-tests --record` writes one:

```
runs/20260825-1024/                           when the run itself was made
  index.md                                    the whole run, for a person
  index.json                                  the whole run, for a program
  20260502T142608Z-c4be69a2-bff2d1eb/         one candidate
    video.mp4                                 the game, with sound
    metadata.json                             verdict and measurements
    deploy.txt                                what the deploy printed
    launch-1.txt launch-2.txt launch-3.txt    what each launch measured
    frame0.png                                the first captured frame
    mask0.png                                 which pixels changed, if any did
```

#### How a candidate folder is named

`20260502T142608Z-c4be69a2-bff2d1eb` has three parts:

| part | meaning |
|---|---|
| `20260502T142608Z` | when that commit was made, in UTC, to the second |
| `c4be69a2` | the **commit** that was checked out and deployed |
| `bff2d1eb` | the **FPGA bitstream** that commit carries, which is the thing actually under test |

The two hexadecimal parts are different kinds of identifier and are easy to
confuse. The first is a commit in this repository. The second is the content
hash of `external/u64.sof` at that commit, from
`git rev-parse --short <commit>:external/u64.sof`. Several commits in a row
normally carry the same bitstream, so the second part is what tells you whether
two folders really tested different hardware.

The timestamp comes first so that an ordinary directory listing puts the
candidates in the order the changes were made. That order is not obvious from
the hashes, and the candidates are not all on one branch.

A sweep rather than a binary search, because the defect's rate varies from one
launch to the next: a candidate can pass by luck, and a binary search would then
step over the answer without saying so. `bisect_bitstreams.sh` still offers the
binary search when the range is large and time matters, and reports any
candidate it could not judge.

Search bitstreams rather than commits either way. Only thirteen distinct ones
exist between v3.14 and v3.15, and different branches carry different bitstream
lineages, so an ordinary `git bisect` over commits spends most of its rounds on
builds whose hardware is identical.

### Record what is on screen now

```sh
tests/bisect/doom/record_run.py --host u64 --seconds 25 -o /tmp/now.mp4
```

Video and audio, muxed into one mp4. `--no-record-audio` for video only.

### Measure without redeploying

```sh
tests/bisect/doom/doom_run.py  --host u64 --tag now
tests/bisect/doom/doom_soak.py --host u64 --label now --seconds 25
```

`doom_run.py --write-golden` records the current first frame as the reference
that later runs are compared against.

## Deploying is volatile, and HDMI goes dark

`deploy_commit.sh` programs the FPGA with `quartus_pgm` and downloads the Nios
application with `nios2-download`. **Nothing is written to flash**, so a power
cycle always restores the flashed firmware and is the recovery for anything
that goes wrong.

**HDMI stays dark for the whole session.** The firmware initialises the HDMI
transmitter only at application start and it does not come back up on an older
core, so anyone at the bench sees no picture until they reboot the machine.
This is expected and is not bitstream corruption; the network VIC stream keeps
working and is what the harness measures. Warn whoever is sitting in front of
the device.

The FPGA is programmed before the application is built and downloaded, on
purpose: `nios2-download` writes into DDR that the bitstream brings up, so
running it against the previous commit's bitstream fails to verify and leaves
the processor paused.

## Files

| File | Purpose |
|---|---|
| `bisect_run.py` | Sweep every bitstream in a range and record the evidence |
| `record_run.py` | Record the device's video and audio to one mp4 |
| `doomlib.py` | Device access and video capture shared by the scripts below |
| `install_assets.py` | Fetch the published release and put `game.reu` on the device |
| `doom_run.py` | Start the game and report whether the engine came up |
| `doom_soak.py` | Measure picture stability and count distinct change-masks |
| `deploy_commit.sh` | Put one commit's FPGA bitstream and application on the device |
| `verdict.sh` | Deploy, launch repeatedly, and decide GOOD or BAD |
| `list_bitstreams.sh` | List the distinct bitstreams in a commit range |
| `bisect_bitstreams.sh` | Binary search those bitstreams |

A fast regression check that needs none of this, and no JTAG, is the
`doom-release` suite: `./run-tests u64 -s doom-release`. It downloads the
release, runs it and checks the engine's own verdict. See
[`tests/e2e/io/c64/doom_release_test.py`](../../e2e/io/c64/doom_release_test.py).
