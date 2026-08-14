# E2E gate run: FAIL

RESULT: FAIL  targets=2  suites=12  ok=8  fail=3  warn=0  skip=1  recoveries=1  exit=1

| Field                      | Value                                                                                                                                                                                                                      |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| commit                     | e2e3bb66030cb406b1f5da567cf280da8c6753ad                                                                                                                                                                                   |
| branch                     | agent/e2e-observability                                                                                                                                                                                                    |
| worktree                   | dirty                                                                                                                                                                                                                      |
| host                       | mickey                                                                                                                                                                                                                     |
| python                     | 3.12.3                                                                                                                                                                                                                     |
| started                    | 2026-08-14 09:47:41                                                                                                                                                                                                        |
| duration                   | 3.411s                                                                                                                                                                                                                     |
| device 127.0.0.1           | Ultimate 64 3.15                                                                                                                                                                                                           |
| device 127.0.0.1@localhost | Ultimate 64 3.15                                                                                                                                                                                                           |
| exit status                | 1: at least one suite failed                                                                                                                                                                                               |
| command                    | `/tmp/e2e-observability-fixture/wrapper.py -j /tmp/e2e-observability-fixture/run --e2e --perf --syslog --syslog-port 49577 --recover-command rm -f /tmp/e2e-observability-fixture/unhealthy 127.0.0.1 127.0.0.1@localhost` |

**Completeness.** This run wrote no closing record for 127.0.0.1@localhost, so it did not finish or was killed, and the counts on the status line above cover the 1 of 2 target(s) that did record one. No closing record for 127.0.0.1/screens/screens/1, 127.0.0.1@localhost/overlay/cut-short/1, so `incomplete` in the table below means the record is absent rather than the suite having a verdict. 1 JSONL line(s) could not be read and were skipped, which is what a writer killed mid-line leaves.

## How to read this

- The line under the title is the whole run, greppable, counted by the runner.
- A suite run is `target/label/suite/attempt` and a check is that plus its
  index. The label is the UI mode for an E2E suite and the category for a perf
  or soak one. A suite run's files are that key with the target dropped and
  `/` written `-`: `overlay-prg-context-menu.log` for its console,
  `-1-screen.txt` under `capture/` for the screen its first attempt left.
- Which section answers what, and what to open when it does not:
  `Verdict` what happened, then `<slug>/<label>-<suite>.jsonl`;
  `Coverage` what did not run or was skipped;
  `Failing checks` one entry each, then that suite's `.log`;
  `Device health` every sweep, then `<slug>/run.jsonl`;
  `Files in this run` everything else this run wrote.
- Below the detail marker: the whole timeline, every check including the
  passing ones and their measurements, and where the time went.
- Nothing here diagnoses anything. Every line is a fact the run recorded.

## Verdict

| Target              | Label   | Suite                | Attempt | Verdict    | Duration | Recoveries | Note                                                          |
| ------------------- | ------- | -------------------- | ------- | ---------- | -------- | ---------- | ------------------------------------------------------------- |
| 127.0.0.1           | overlay | held                 | 1       | OK         | 0.036s   | 0          | -                                                             |
| 127.0.0.1           | overlay | broken               | 1       | FAIL       | 0.042s   | 0          | -                                                             |
| 127.0.0.1           | overlay | raised               | 1       | FAIL       | 0.096s   | 0          | -                                                             |
| 127.0.0.1           | overlay | flaky                | 1       | FAIL       | 0.035s   | 0          | -                                                             |
| 127.0.0.1           | overlay | flaky                | 2       | OK         | 0.035s   | 1          | -                                                             |
| 127.0.0.1           | overlay | noisy                | 1       | FAIL       | 0.194s   | 0          | -                                                             |
| 127.0.0.1           | screens | screens              | 1       | incomplete | -        | 0          | -                                                             |
| 127.0.0.1           | overlay | browse               | 1       | OK         | 0.136s   | 0          | -                                                             |
| 127.0.0.1           | overlay | menu-left-open       | 1       | OK         | 0.035s   | 0          | -                                                             |
| 127.0.0.1           | overlay | menu-closed-again    | 1       | OK         | 0.034s   | 0          | -                                                             |
| 127.0.0.1           | overlay | leaves-things-behind | 1       | OK         | 0.090s   | 0          | -                                                             |
| 127.0.0.1           | overlay | missing-file         | 1       | SKIP       | 0.000s   | 0          | missing /tmp/e2e-observability-fixture/suites/missing_file.py |
| 127.0.0.1           | overlay | cut-short            | 1       | OK         | 0.032s   | 0          | -                                                             |
| 127.0.0.1           | perf    | a-benchmark          | 1       | OK         | 0.039s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | held                 | 1       | OK         | 0.031s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | broken               | 1       | FAIL       | 0.035s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | raised               | 1       | FAIL       | 0.101s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | flaky                | 1       | FAIL       | 0.036s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | flaky                | 2       | OK         | 0.039s   | 1          | -                                                             |
| 127.0.0.1@localhost | overlay | noisy                | 1       | FAIL       | 0.198s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | browse               | 1       | FAIL       | 0.665s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | menu-left-open       | 1       | OK         | 0.042s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | menu-closed-again    | 1       | OK         | 0.039s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | leaves-things-behind | 1       | OK         | 0.110s   | 0          | -                                                             |
| 127.0.0.1@localhost | overlay | missing-file         | 1       | SKIP       | 0.000s   | 0          | missing /tmp/e2e-observability-fixture/suites/missing_file.py |
| 127.0.0.1@localhost | overlay | cut-short            | 1       | incomplete | -        | 0          | -                                                             |

## Coverage

- 22 of 24 planned suite runs completed.
- No firmware fixes were assumed; every check tagged with a missing fix reported SKIP.

Registered suites this run did not run:

| Target              | Suite         | Category | Reason |
| ------------------- | ------------- | -------- | ------ |
| 127.0.0.1           | operator-only | e2e      | manual |
| 127.0.0.1@localhost | operator-only | e2e      | manual |

Checks that reported SKIP:

| Target              | Suite  | Checks | Reason                                                                  |
| ------------------- | ------ | ------ | ----------------------------------------------------------------------- |
| 127.0.0.1           | broken | 1      | needs the ftp-listing-full-length fix, which this machine does not have |
| 127.0.0.1@localhost | broken | 1      | needs the ftp-listing-full-length fix, which this machine does not have |

## What this run changed

What the action log says the run did to the device and, where a mutation has an undoing request, whether that request was made. A suite may leave something behind on purpose, so this is a list for a reader to judge rather than a verdict.

| Target              | Suite run                                          | What              | Where                                             |
| ------------------- | -------------------------------------------------- | ----------------- | ------------------------------------------------- |
| 127.0.0.1           | 127.0.0.1/overlay/leaves-things-behind/1           | mount not undone  | /v1/drives/a                                      |
| 127.0.0.1@localhost | 127.0.0.1@localhost/overlay/leaves-things-behind/1 | mount not undone  | /v1/drives/a                                      |
| 127.0.0.1           | 127.0.0.1/overlay/leaves-things-behind/1           | written 1 time(s) | /v1/configs/Network Settings/Log to Syslog Server |
| 127.0.0.1@localhost | 127.0.0.1@localhost/overlay/leaves-things-behind/1 | written 1 time(s) | /v1/configs/Network Settings/Log to Syslog Server |

## Failing checks

### 127.0.0.1/overlay/broken/1/1 - the row survives a redraw

`FAIL` after 0.000s, at 2026-08-14 09:47:41, reported `0 rows, expected 20`.

- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.
- First failure: no other check in this suite run failed before it.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/capture/overlay-broken-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/capture/overlay-broken-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s broken --mode overlay`

Source: `/tmp/e2e-observability-fixture/suites/broken.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1/overlay-broken.log`:

```
[01] the row survives a redraw ... FAIL (0 rows, expected 20, 0.000s)
[02] the name is listed in full ... SKIP (needs the ftp-listing-full-length fix, which this machine does not have, 0.000s)
```

### 127.0.0.1/overlay/flaky/1/1 - the device is well

`FAIL` after 0.000s, at 2026-08-14 09:47:42, reported `the listener is gone`.

- Passed on retry: this check passed on another attempt.
- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.
- Passed elsewhere: the same check OK on 127.0.0.1@localhost.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/capture/overlay-flaky-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/capture/overlay-flaky-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s flaky --mode overlay`

Source: `/tmp/e2e-observability-fixture/suites/flaky.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1/overlay-flaky.log`, which holds all 2 attempts appended in order:

```
[01] the device is well ... FAIL (the listener is gone, 0.000s)
[01] the device is well ... OK (recovered, 0.000s)
```

### 127.0.0.1/overlay/noisy/1/1 - the drive answers

`FAIL` after 0.101s, at 2026-08-14 09:47:42, reported `the drive did not answer`.

- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/capture/overlay-noisy-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/capture/overlay-noisy-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s noisy --mode overlay`

Source: `/tmp/e2e-observability-fixture/suites/noisy.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1/overlay-noisy.log`:

```
[01] the drive answers ... FAIL (the drive did not answer, 0.100s)
```

### 127.0.0.1@localhost/overlay/broken/1/1 - the row survives a redraw

`FAIL` after 0.000s, at 2026-08-14 09:47:43, reported `0 rows, expected 20`.

- Failed elsewhere: the same check FAIL on 127.0.0.1.
- First failure: no other check in this suite run failed before it.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/capture/overlay-broken-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-broken-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s broken --mode overlay`

Source: `/tmp/e2e-observability-fixture/suites/broken.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1-at-localhost/overlay-broken.log`:

```
[01] the row survives a redraw ... FAIL (0 rows, expected 20, 0.000s)
[02] the name is listed in full ... SKIP (needs the ftp-listing-full-length fix, which this machine does not have, 0.000s)
```

### 127.0.0.1@localhost/overlay/flaky/1/1 - the device is well

`FAIL` after 0.000s, at 2026-08-14 09:47:43, reported `the listener is gone`.

- Passed on retry: this check passed on another attempt.
- Failed elsewhere: the same check FAIL on 127.0.0.1.
- Passed elsewhere: the same check OK on 127.0.0.1.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/capture/overlay-flaky-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-flaky-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s flaky --mode overlay`

Source: `/tmp/e2e-observability-fixture/suites/flaky.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1-at-localhost/overlay-flaky.log`, which holds all 2 attempts appended in order:

```
[01] the device is well ... FAIL (the listener is gone, 0.000s)
[01] the device is well ... OK (recovered, 0.000s)
```

### 127.0.0.1@localhost/overlay/noisy/1/1 - the drive answers

`FAIL` after 0.100s, at 2026-08-14 09:47:43, reported `the drive did not answer`.

- Failed elsewhere: the same check FAIL on 127.0.0.1.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/capture/overlay-noisy-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-noisy-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s noisy --mode overlay`

Source: `/tmp/e2e-observability-fixture/suites/noisy.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1-at-localhost/overlay-noisy.log`:

```
[01] the drive answers ... FAIL (the drive did not answer, 0.100s)
```

### 127.0.0.1/overlay/raised/1 - the suite itself

`FAIL`, with no failing check of its own.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/capture/overlay-raised-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/capture/overlay-raised-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s raised --mode overlay`

Last 4 line(s) of `127.0.0.1/overlay-raised.log`:

```
[01] the device answers ... Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture/suites/raised.py", line 16, in <module>
    raise RuntimeError('the device stopped answering mid-check')
RuntimeError: the device stopped answering mid-check
```

### 127.0.0.1/screens/screens/1 - the suite itself

`incomplete`, with no failing check of its own.

Reproduce: `./run-tests -H 127.0.0.1 -s screens`

### 127.0.0.1@localhost/overlay/raised/1 - the suite itself

`FAIL`, with no failing check of its own.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/capture/overlay-raised-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-raised-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s raised --mode overlay`

Last 4 line(s) of `127.0.0.1-at-localhost/overlay-raised.log`:

```
[01] the device answers ... Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture/suites/raised.py", line 16, in <module>
    raise RuntimeError('the device stopped answering mid-check')
RuntimeError: the device stopped answering mid-check
```

### 127.0.0.1@localhost/overlay/browse/1 - the suite itself

`FAIL`, with no failing check of its own.

The menu screen when this suite ended (`127.0.0.1-at-localhost/capture/overlay-browse-1-screen.txt`):

```
Ultimate 64 menu
key 3
row 02
row 03
row 04
row 05
row 06
row 07
row 08
row 09
row 10
row 11
row 12
row 13
row 14
row 15
row 16
row 17
row 18
row 19
row 20
row 21
row 22
row 23
row 24
```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-browse-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s browse --mode overlay`

Last 12 line(s) of `127.0.0.1-at-localhost/overlay-browse.log`:

```
Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture/suites/browse.py", line 18, in <module>
    backend = ui_backend.make_backend('overlay', ARGS.host,
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/chris/dev/c64/1541u-e2e-obs/tests/e2e/lib/ui_backend.py", line 1947, in make_backend
    return RestBackend(host, password, timeout, interface_type=_MODE_INTERFACE_TYPE[mode])
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/chris/dev/c64/1541u-e2e-obs/tests/e2e/lib/ui_backend.py", line 1006, in __init__
    close_host_menu(self.input_host, password, timeout)
  File "/home/chris/dev/c64/1541u-e2e-obs/tests/e2e/lib/ui_backend.py", line 174, in close_host_menu
    raise Failure(
report.Failure: the menu on localhost would not close, so keys sent to it would be consumed by that menu instead of reaching the cartridge
```

### 127.0.0.1@localhost/overlay/cut-short/1 - the suite itself

`incomplete`, with no failing check of its own.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s cut-short`

Last 1 line(s) of `127.0.0.1-at-localhost/overlay-cut-short.log`:

```
[01] the first half ... OK (0.000s)
```

## Device health

### 127.0.0.1

| Sweep                                 | Verdict  | ping | rest | ftp  | telnet | ident | dma | heap     | raster | jiffy |
| ------------------------------------- | -------- | ---- | ---- | ---- | ------ | ----- | --- | -------- | ------ | ----- |
| held                                  | OK       | 2ms  | 0ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| broken                                | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| broken: after failure,                | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised                                | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised: after failure,                | OK       | 2ms  | 0ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| flaky                                 | OK       | 2ms  | 0ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| flaky: after failure,                 | DEGRADED | 2ms  | 0ms  | FAIL | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| flaky: after failure, after recovery, | OK       | 2ms  | 0ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| flaky                                 | OK       | 2ms  | 0ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| noisy                                 | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| noisy: after failure,                 | OK       | 4ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 3ms    | 3ms   |
| browse                                | OK       | 4ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| menu-left-open                        | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| menu-closed-again                     | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | skip   | skip  |
| leaves-things-behind                  | OK       | 2ms  | 0ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| cut-short                             | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |

### 127.0.0.1@localhost

| Sweep                                 | Verdict  | ping | rest | ftp  | telnet | ident | dma | heap     | raster | jiffy |
| ------------------------------------- | -------- | ---- | ---- | ---- | ------ | ----- | --- | -------- | ------ | ----- |
| held                                  | OK       | 2ms  | 0ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| broken                                | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| broken: after failure,                | OK       | 2ms  | 0ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised                                | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised: after failure,                | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| flaky                                 | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| flaky: after failure,                 | DEGRADED | 4ms  | 1ms  | FAIL | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| flaky: after failure, after recovery, | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| flaky                                 | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| noisy                                 | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| noisy: after failure,                 | OK       | 4ms  | 1ms  | 0ms  | 0ms    | 2ms   | 0ms | 1500000B | 1ms    | 1ms   |
| browse                                | OK       | 4ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| browse: after failure,                | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | skip   | skip  |
| menu-left-open                        | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | skip   | skip  |
| menu-closed-again                     | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| leaves-things-behind                  | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 0ms   | 0ms | 1500000B | 1ms    | 1ms   |
| cut-short                             | OK       | 2ms  | 1ms  | 0ms  | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |

## Files in this run

A capture file's name is its suite run's key with `/` written `-` and the target dropped, because the file already sits under that target's directory.

Reaching one of these from a build page is a download and an unzip: GitHub serves no URL for a single file inside a zipped artifact, so the artifact link on the build page is the last click there is.

| Path                                                         | Bytes | What it is                                                                                                      |
| ------------------------------------------------------------ | ----- | --------------------------------------------------------------------------------------------------------------- |
| `index.md`                                                   | -     | this report, written by tools/e2e_report.py                                                                     |
| `run.jsonl`                                                  | 3879  | the run's own records: the plan, the health sweeps, the suite verdicts and the run result, written by run-tests |
| `run.log`                                                    | 19109 | run-tests' own console output                                                                                   |
| `127.0.0.1/overlay-broken.jsonl`                             | 527   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-broken.log`                               | 193   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-browse.jsonl`                             | 403   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-browse.log`                               | 47    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-cut-short.jsonl`                          | 417   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-cut-short.log`                            | 73    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-flaky.jsonl`                              | 449   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-flaky.log`                                | 115   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-held.jsonl`                               | 463   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-held.log`                                 | 126   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-leaves-things-behind.jsonl`               | 975   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-leaves-things-behind.log`                 | 77    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-menu-closed-again.jsonl`                  | 217   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-menu-closed-again.log`                    | 37    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-menu-left-open.jsonl`                     | 225   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-menu-left-open.log`                       | 47    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-noisy.jsonl`                              | 236   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-noisy.log`                                | 67    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/overlay-raised.jsonl`                             | 0     | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/overlay-raised.log`                               | 260   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/perf-a-benchmark.jsonl`                           | 244   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1/perf-a-benchmark.log`                             | 72    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1/run.jsonl`                                        | 25258 | the run's own records: the plan, the health sweeps, the suite verdicts and the run result, written by run-tests |
| `127.0.0.1/run.log`                                          | 5996  | run-tests' own console output                                                                                   |
| `127.0.0.1/screens.jsonl`                                    | 10502 | every distinct screen the harness read, as text and as raw bytes                                                |
| `127.0.0.1/syslog.txt`                                       | 316   | the device's own log, as the collector received it, best effort and incomplete by construction                  |
| `127.0.0.1/capture/overlay-broken-1-screen.bin`              | 1000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1/capture/overlay-broken-1-screen.txt`              | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1/capture/overlay-broken-1-state.json`              | 547   | the drive state and free heap when a suite failed                                                               |
| `127.0.0.1/capture/overlay-flaky-1-screen.bin`               | 1000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1/capture/overlay-flaky-1-screen.txt`               | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1/capture/overlay-flaky-1-state.json`               | 547   | the drive state and free heap when a suite failed                                                               |
| `127.0.0.1/capture/overlay-noisy-1-screen.bin`               | 1000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1/capture/overlay-noisy-1-screen.txt`               | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1/capture/overlay-noisy-1-state.json`               | 547   | the drive state and free heap when a suite failed                                                               |
| `127.0.0.1/capture/overlay-raised-1-screen.bin`              | 1000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1/capture/overlay-raised-1-screen.txt`              | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1/capture/overlay-raised-1-state.json`              | 547   | the drive state and free heap when a suite failed                                                               |
| `127.0.0.1-at-localhost/overlay-broken.jsonl`                | 546   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-broken.log`                  | 193   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-browse.jsonl`                | 191   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-browse.log`                  | 925   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-cut-short.jsonl`             | 252   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-cut-short.log`               | 36    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-flaky.jsonl`                 | 469   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-flaky.log`                   | 115   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-held.jsonl`                  | 485   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-held.log`                    | 126   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-leaves-things-behind.jsonl`  | 1016  | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-leaves-things-behind.log`    | 77    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-menu-closed-again.jsonl`     | 227   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-menu-closed-again.log`       | 37    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-menu-left-open.jsonl`        | 232   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-menu-left-open.log`          | 47    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-noisy.jsonl`                 | 245   | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-noisy.log`                   | 67    | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/overlay-raised.jsonl`                | 0     | one suite run's checks, scenarios and device actions                                                            |
| `127.0.0.1-at-localhost/overlay-raised.log`                  | 260   | that suite run's console output, stderr merged in, ANSI stripped                                                |
| `127.0.0.1-at-localhost/run.jsonl`                           | 22997 | the run's own records: the plan, the health sweeps, the suite verdicts and the run result, written by run-tests |
| `127.0.0.1-at-localhost/run.log`                             | 5004  | run-tests' own console output                                                                                   |
| `127.0.0.1-at-localhost/screens.jsonl`                       | 0     | every distinct screen the harness read, as text and as raw bytes                                                |
| `127.0.0.1-at-localhost/capture/overlay-broken-1-screen.bin` | 1000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-broken-1-screen.txt` | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1-at-localhost/capture/overlay-broken-1-state.json` | 561   | the drive state and free heap when a suite failed                                                               |
| `127.0.0.1-at-localhost/capture/overlay-browse-1-screen.bin` | 2000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-browse-1-screen.txt` | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1-at-localhost/capture/overlay-browse-1-state.json` | 565   | the drive state and free heap when a suite failed                                                               |
| `127.0.0.1-at-localhost/capture/overlay-flaky-1-screen.bin`  | 1000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-flaky-1-screen.txt`  | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1-at-localhost/capture/overlay-flaky-1-state.json`  | 561   | the drive state and free heap when a suite failed                                                               |
| `127.0.0.1-at-localhost/capture/overlay-noisy-1-screen.bin`  | 1000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-noisy-1-screen.txt`  | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1-at-localhost/capture/overlay-noisy-1-state.json`  | 561   | the drive state and free heap when a suite failed                                                               |
| `127.0.0.1-at-localhost/capture/overlay-raised-1-screen.bin` | 1000  | the same screen, as the device's own bytes                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-raised-1-screen.txt` | 1025  | the screen a failing suite left, as text                                                                        |
| `127.0.0.1-at-localhost/capture/overlay-raised-1-state.json` | 561   | the drive state and free heap when a suite failed                                                               |

<!-- detail -->

## Timeline

+00:00  127.0.0.1 warning: device log: this device is not configured to send its log anywhere (at the start of the run)
+00:00  127.0.0.1 GET /v1/machine:menu_screen -> 404: Menu screen unavailable.  (this request is made 20 times in this run and is shown twice)
+00:00  127.0.0.1 sweep held: OK
+00:00  127.0.0.1/overlay/held/1 started
+00:00  127.0.0.1/overlay/held/1 OK
+00:00  127.0.0.1 GET /v1/machine:menu_screen -> 404: Menu screen unavailable.  (this request is made 20 times in this run and is shown twice)
+00:00  127.0.0.1 sweep broken: OK
+00:00  127.0.0.1/overlay/broken/1 started
+00:00  127.0.0.1/overlay/broken/1/1 FAIL the row survives a redraw
+00:00  127.0.0.1/overlay/broken/1 FAIL
+00:00  127.0.0.1/overlay/broken/1 device state captured
+00:00  127.0.0.1 sweep broken: after failure,: OK
+00:00  127.0.0.1 sweep raised: OK
+00:00  127.0.0.1/overlay/raised/1 started
+00:00  127.0.0.1/overlay/raised/1 FAIL
+00:00  127.0.0.1/overlay/raised/1 device state captured
+00:00  127.0.0.1 sweep raised: after failure,: OK
+00:00  127.0.0.1 sweep flaky: OK
+00:00  127.0.0.1/overlay/flaky/1 started
+00:00  127.0.0.1/overlay/flaky/1/1 FAIL the device is well
+00:00  127.0.0.1/overlay/flaky/1 FAIL
+00:00  127.0.0.1/overlay/flaky/1 device state captured
+00:00  127.0.0.1 sweep flaky: after failure,: DEGRADED
+00:00  127.0.0.1 sweep flaky: after failure, after recovery,: OK
+00:00  127.0.0.1 sweep flaky: OK
+00:00  127.0.0.1/overlay/flaky/2 started
+00:00  127.0.0.1 was recovered 1 time(s) around flaky
+00:00  127.0.0.1/overlay/flaky/2 OK
+00:00  127.0.0.1 sweep noisy: OK
+00:00  127.0.0.1/overlay/noisy/1 started
+00:00  127.0.0.1 restarted, seen in its own log
+00:00  127.0.0.1/overlay/noisy/1/1 FAIL the drive answers
+00:00  127.0.0.1/overlay/noisy/1 FAIL
+00:00  127.0.0.1/overlay/noisy/1 device state captured
+00:00  127.0.0.1 sweep noisy: after failure,: OK
+00:00  127.0.0.1 sweep browse: OK
+00:00  127.0.0.1/overlay/browse/1 started
+00:00  127.0.0.1/overlay/browse/1 POST /v1/machine:input
+00:00  127.0.0.1/screens/screens/1 started
+00:00  127.0.0.1/screens/screens/1 incomplete
+00:00  127.0.0.1/overlay/browse/1 OK
+00:00  127.0.0.1 sweep menu-left-open: OK
+00:00  127.0.0.1/overlay/menu-left-open/1 started
+00:00  127.0.0.1/overlay/menu-left-open/1 OK
+00:00  127.0.0.1 sweep menu-closed-again: OK
+00:00  127.0.0.1/overlay/menu-closed-again/1 started
+00:00  127.0.0.1/overlay/menu-closed-again/1 OK
+00:00  127.0.0.1 sweep leaves-things-behind: OK
+00:00  127.0.0.1/overlay/leaves-things-behind/1 started
+00:00  127.0.0.1/overlay/leaves-things-behind/1 PUT /v1/drives/a:mount {'image': '/Usb0/game.d64'}
+00:00  127.0.0.1/overlay/leaves-things-behind/1 PUT /v1/configs/Network%20Settings/Log%20to%20Syslog%20Server {'value': '192.168.1.2:5514'}
+00:00  127.0.0.1/overlay/leaves-things-behind/1 OK
+00:00  127.0.0.1/overlay/missing-file/1 started
+00:00  127.0.0.1/overlay/missing-file/1 SKIP: missing /tmp/e2e-observability-fixture/suites/missing_file.py
+00:00  127.0.0.1 sweep cut-short: OK
+00:00  127.0.0.1/overlay/cut-short/1 started
+00:01  127.0.0.1/overlay/cut-short/1 OK
+00:01  5 device requests (GET, POST, PUT)
+00:01  127.0.0.1/perf/a-benchmark/1 started
+00:01  127.0.0.1/perf/a-benchmark/1 OK
+00:01  127.0.0.1@localhost GET /v1/machine:menu_screen -> 404: Menu screen unavailable.  (this request is made 18 times in this run and is shown twice)
+00:01  127.0.0.1@localhost sweep held: OK
+00:01  127.0.0.1@localhost/overlay/held/1 started
+00:01  127.0.0.1@localhost/overlay/held/1 OK
+00:01  127.0.0.1@localhost GET /v1/machine:menu_screen -> 404: Menu screen unavailable.  (this request is made 18 times in this run and is shown twice)
+00:01  127.0.0.1@localhost sweep broken: OK
+00:01  127.0.0.1@localhost/overlay/broken/1 started
+00:01  127.0.0.1@localhost/overlay/broken/1/1 FAIL the row survives a redraw
+00:01  127.0.0.1@localhost/overlay/broken/1 FAIL
+00:01  127.0.0.1@localhost/overlay/broken/1 device state captured
+00:01  127.0.0.1@localhost sweep broken: after failure,: OK
+00:01  127.0.0.1@localhost sweep raised: OK
+00:01  127.0.0.1@localhost/overlay/raised/1 started
+00:01  127.0.0.1@localhost/overlay/raised/1 FAIL
+00:01  127.0.0.1@localhost/overlay/raised/1 device state captured
+00:01  127.0.0.1@localhost sweep raised: after failure,: OK
+00:01  127.0.0.1@localhost sweep flaky: OK
+00:01  127.0.0.1@localhost/overlay/flaky/1 started
+00:01  127.0.0.1@localhost/overlay/flaky/1/1 FAIL the device is well
+00:01  127.0.0.1@localhost/overlay/flaky/1 FAIL
+00:01  127.0.0.1@localhost/overlay/flaky/1 device state captured
+00:01  127.0.0.1@localhost sweep flaky: after failure,: DEGRADED
+00:01  127.0.0.1@localhost sweep flaky: after failure, after recovery,: OK
+00:01  127.0.0.1@localhost sweep flaky: OK
+00:01  127.0.0.1@localhost/overlay/flaky/2 started
+00:02  127.0.0.1@localhost was recovered 1 time(s) around flaky
+00:02  127.0.0.1@localhost/overlay/flaky/2 OK
+00:02  127.0.0.1@localhost sweep noisy: OK
+00:02  127.0.0.1@localhost/overlay/noisy/1 started
+00:02  127.0.0.1 restarted, seen in its own log
+00:02  127.0.0.1@localhost/overlay/noisy/1/1 FAIL the drive answers
+00:02  127.0.0.1@localhost/overlay/noisy/1 FAIL
+00:02  127.0.0.1@localhost/overlay/noisy/1 device state captured
+00:02  127.0.0.1@localhost sweep noisy: after failure,: OK
+00:02  127.0.0.1@localhost sweep browse: OK
+00:02  127.0.0.1@localhost/overlay/browse/1 started
+00:02  127.0.0.1@localhost/overlay/browse/1 PUT /v1/machine:menu_button
+00:02  127.0.0.1@localhost/overlay/browse/1 FAIL
+00:02  127.0.0.1@localhost/overlay/browse/1 device state captured
+00:02  127.0.0.1@localhost sweep browse: after failure,: OK
+00:02  127.0.0.1@localhost sweep menu-left-open: OK
+00:02  127.0.0.1@localhost/overlay/menu-left-open/1 started
+00:02  127.0.0.1@localhost/overlay/menu-left-open/1 OK
+00:02  127.0.0.1@localhost sweep menu-closed-again: OK
+00:02  127.0.0.1@localhost/overlay/menu-closed-again/1 started
+00:03  127.0.0.1@localhost/overlay/menu-closed-again/1 OK
+00:03  127.0.0.1@localhost sweep leaves-things-behind: OK
+00:03  127.0.0.1@localhost/overlay/leaves-things-behind/1 started
+00:03  127.0.0.1@localhost/overlay/leaves-things-behind/1 PUT /v1/drives/a:mount {'image': '/Usb0/game.d64'}
+00:03  127.0.0.1@localhost/overlay/leaves-things-behind/1 PUT /v1/configs/Network%20Settings/Log%20to%20Syslog%20Server {'value': '192.168.1.2:5514'}
+00:03  127.0.0.1@localhost/overlay/leaves-things-behind/1 OK
+00:03  127.0.0.1@localhost/overlay/missing-file/1 started
+00:03  127.0.0.1@localhost/overlay/missing-file/1 SKIP: missing /tmp/e2e-observability-fixture/suites/missing_file.py
+00:03  127.0.0.1@localhost sweep cut-short: OK
+00:03  127.0.0.1@localhost/overlay/cut-short/1 started
+00:03  127.0.0.1@localhost/overlay/cut-short/1 incomplete

## Checks

### 127.0.0.1/overlay/held/1

**the ordinary case**

| # | Check                       | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the listing is complete     | OK      | 0.000s   | 2026-08-14 09:47:41 | 2026-08-14 09:47:41 | 20 rows  |
| 2 | the first row is the header | OK      | 0.000s   | 2026-08-14 09:47:41 | 2026-08-14 09:47:41 | -        |

### 127.0.0.1/overlay/broken/1

| # | Check                      | Verdict | Duration | Opened at           | Closed at           | Reported                                                                |
| - | -------------------------- | ------- | -------- | ------------------- | ------------------- | ----------------------------------------------------------------------- |
| 1 | the row survives a redraw  | FAIL    | 0.000s   | 2026-08-14 09:47:41 | 2026-08-14 09:47:41 | 0 rows, expected 20                                                     |
| 2 | the name is listed in full | SKIP    | 0.000s   | 2026-08-14 09:47:41 | 2026-08-14 09:47:41 | needs the ftp-listing-full-length fix, which this machine does not have |

### 127.0.0.1/overlay/flaky/1

| # | Check              | Verdict | Duration | Opened at           | Closed at           | Reported             |
| - | ------------------ | ------- | -------- | ------------------- | ------------------- | -------------------- |
| 1 | the device is well | FAIL    | 0.000s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | the listener is gone |

### 127.0.0.1/overlay/flaky/2

| # | Check              | Verdict | Duration | Opened at           | Closed at           | Reported  |
| - | ------------------ | ------- | -------- | ------------------- | ------------------- | --------- |
| 1 | the device is well | OK      | 0.000s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | recovered |

### 127.0.0.1/overlay/noisy/1

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | the drive answers | FAIL    | 0.101s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | the drive did not answer |

### 127.0.0.1/overlay/browse/1

| # | Check            | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | ---------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the cursor moves | OK      | 0.016s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | one row  |

### 127.0.0.1/overlay/menu-left-open/1

| # | Check          | Verdict | Duration | Opened at           | Closed at           | Reported  |
| - | -------------- | ------- | -------- | ------------------- | ------------------- | --------- |
| 1 | the menu opens | OK      | 0.000s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | on screen |

### 127.0.0.1/overlay/menu-closed-again/1

| # | Check           | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the menu closes | OK      | 0.000s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | -        |

### 127.0.0.1/overlay/leaves-things-behind/1

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the image mounts  | OK      | 0.022s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | -        |
| 2 | the setting takes | OK      | 0.001s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | -        |

### 127.0.0.1/overlay/cut-short/1

| # | Check           | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the first half  | OK      | 0.000s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | -        |
| 2 | the second half | OK      | 0.000s   | 2026-08-14 09:47:42 | 2026-08-14 09:47:42 | -        |

### 127.0.0.1/perf/a-benchmark/1

| # | Check                    | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ------------------------ | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | typing reaches the field | OK      | 0.000s   | 2026-08-14 09:47:43 | 2026-08-14 09:47:43 | 11.2 characters a second |

### 127.0.0.1@localhost/overlay/held/1

**the ordinary case**

| # | Check                       | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the listing is complete     | OK      | 0.000s   | 2026-08-14 09:47:43 | 2026-08-14 09:47:43 | 20 rows  |
| 2 | the first row is the header | OK      | 0.000s   | 2026-08-14 09:47:43 | 2026-08-14 09:47:43 | -        |

### 127.0.0.1@localhost/overlay/broken/1

| # | Check                      | Verdict | Duration | Opened at           | Closed at           | Reported                                                                |
| - | -------------------------- | ------- | -------- | ------------------- | ------------------- | ----------------------------------------------------------------------- |
| 1 | the row survives a redraw  | FAIL    | 0.000s   | 2026-08-14 09:47:43 | 2026-08-14 09:47:43 | 0 rows, expected 20                                                     |
| 2 | the name is listed in full | SKIP    | 0.000s   | 2026-08-14 09:47:43 | 2026-08-14 09:47:43 | needs the ftp-listing-full-length fix, which this machine does not have |

### 127.0.0.1@localhost/overlay/flaky/1

| # | Check              | Verdict | Duration | Opened at           | Closed at           | Reported             |
| - | ------------------ | ------- | -------- | ------------------- | ------------------- | -------------------- |
| 1 | the device is well | FAIL    | 0.000s   | 2026-08-14 09:47:43 | 2026-08-14 09:47:43 | the listener is gone |

### 127.0.0.1@localhost/overlay/flaky/2

| # | Check              | Verdict | Duration | Opened at           | Closed at           | Reported  |
| - | ------------------ | ------- | -------- | ------------------- | ------------------- | --------- |
| 1 | the device is well | OK      | 0.000s   | 2026-08-14 09:47:43 | 2026-08-14 09:47:43 | recovered |

### 127.0.0.1@localhost/overlay/noisy/1

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | the drive answers | FAIL    | 0.100s   | 2026-08-14 09:47:43 | 2026-08-14 09:47:43 | the drive did not answer |

### 127.0.0.1@localhost/overlay/menu-left-open/1

| # | Check          | Verdict | Duration | Opened at           | Closed at           | Reported  |
| - | -------------- | ------- | -------- | ------------------- | ------------------- | --------- |
| 1 | the menu opens | OK      | 0.000s   | 2026-08-14 09:47:44 | 2026-08-14 09:47:44 | on screen |

### 127.0.0.1@localhost/overlay/menu-closed-again/1

| # | Check           | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the menu closes | OK      | 0.000s   | 2026-08-14 09:47:44 | 2026-08-14 09:47:44 | -        |

### 127.0.0.1@localhost/overlay/leaves-things-behind/1

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the image mounts  | OK      | 0.031s   | 2026-08-14 09:47:44 | 2026-08-14 09:47:44 | -        |
| 2 | the setting takes | OK      | 0.001s   | 2026-08-14 09:47:44 | 2026-08-14 09:47:44 | -        |

### 127.0.0.1@localhost/overlay/cut-short/1

| # | Check          | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | -------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the first half | OK      | 0.000s   | 2026-08-14 09:47:44 | 2026-08-14 09:47:44 | -        |

## Where the time went

Slowest suite runs:

| Suite run                                          | Duration |
| -------------------------------------------------- | -------- |
| 127.0.0.1@localhost/overlay/browse/1               | 0.665s   |
| 127.0.0.1@localhost/overlay/noisy/1                | 0.198s   |
| 127.0.0.1/overlay/noisy/1                          | 0.194s   |
| 127.0.0.1/overlay/browse/1                         | 0.136s   |
| 127.0.0.1@localhost/overlay/leaves-things-behind/1 | 0.110s   |
| 127.0.0.1@localhost/overlay/raised/1               | 0.101s   |
| 127.0.0.1/overlay/raised/1                         | 0.096s   |
| 127.0.0.1/overlay/leaves-things-behind/1           | 0.090s   |
| 127.0.0.1/overlay/broken/1                         | 0.042s   |
| 127.0.0.1@localhost/overlay/menu-left-open/1       | 0.042s   |

Slowest checks:

| Check                                                | Label              | Duration |
| ---------------------------------------------------- | ------------------ | -------- |
| 127.0.0.1/overlay/noisy/1/1                          | the drive answers  | 0.101s   |
| 127.0.0.1@localhost/overlay/noisy/1/1                | the drive answers  | 0.100s   |
| 127.0.0.1@localhost/overlay/leaves-things-behind/1/1 | the image mounts   | 0.031s   |
| 127.0.0.1/overlay/leaves-things-behind/1/1           | the image mounts   | 0.022s   |
| 127.0.0.1/overlay/browse/1/1                         | the cursor moves   | 0.016s   |
| 127.0.0.1@localhost/overlay/leaves-things-behind/1/2 | the setting takes  | 0.001s   |
| 127.0.0.1/overlay/leaves-things-behind/1/2           | the setting takes  | 0.001s   |
| 127.0.0.1/overlay/flaky/1/1                          | the device is well | 0.000s   |
| 127.0.0.1/overlay/menu-left-open/1/1                 | the menu opens     | 0.000s   |
| 127.0.0.1@localhost/overlay/flaky/1/1                | the device is well | 0.000s   |

## Device log

The device log is best-effort and incomplete by construction. It is UDP with no retransmission, the firmware's 16 KB forwarding buffer discards itself whole on overflow, output is throttled to about 200 lines a second, and an assertion failure never arrives at all because the task that would send it stops running. A line's time is when this host received it, which lags when the firmware printed it by an unbounded amount, so these are lines received during a check and not lines the device produced during it.

### 127.0.0.1

6 line(s) received, from `127.0.0.1/syslog.txt`.

**127.0.0.1/overlay/noisy/1/1**, from the end of the check before it:

```
All linked modules have been initialized and are now running.
1541: seek track 18
1541: no answer from the drive
```
