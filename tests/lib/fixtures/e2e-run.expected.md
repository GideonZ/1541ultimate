# E2E gate run: FAIL

RESULT: FAIL  targets=2  suites=12  ok=8  fail=3  warn=0  skip=1  recoveries=1  retried=4  exit=3

| Field                      | Value                                                                                                                                                                                                                                             |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| commit                     | ece5489629b612308694697e306c08cef536c2db                                                                                                                                                                                                          |
| branch                     | fix/e2e-telnet-refresh-and-joystick                                                                                                                                                                                                               |
| worktree                   | clean                                                                                                                                                                                                                                             |
| host                       | mickey                                                                                                                                                                                                                                            |
| python                     | 3.12.3                                                                                                                                                                                                                                            |
| started                    | 2026-09-02 22:51:24                                                                                                                                                                                                                               |
| duration                   | 16.9s                                                                                                                                                                                                                                             |
| device 127.0.0.1           | Ultimate 64 3.15                                                                                                                                                                                                                                  |
| device 127.0.0.1@localhost | Ultimate 64 3.15                                                                                                                                                                                                                                  |
| exit status                | 3: a suite failed every attempt it was given                                                                                                                                                                                                      |
| command                    | `/tmp/e2e-observability-fixture-cfpfp5j1/wrapper.py -o /tmp/e2e-observability-fixture-cfpfp5j1/run --e2e --perf --syslog --syslog-port 0 --recover-command rm -f /tmp/e2e-observability-fixture-cfpfp5j1/unhealthy 127.0.0.1 127.0.0.1@localhost` |

**Completeness.** This run wrote no closing record for 127.0.0.1@localhost, so it did not finish or was killed, and the counts on the status line above cover the 1 of 2 target(s) that did record one. No closing record for 127.0.0.1@localhost/overlay/cut-short/1, so `incomplete` in the table below means the record is absent rather than the suite having a verdict. 1 JSONL line(s) could not be read and were skipped, which is what a writer killed mid-line leaves.

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

| Target              | Label   | Suite                | Attempt | Verdict    | Duration | Recoveries | Note                                                                   |
| ------------------- | ------- | -------------------- | ------- | ---------- | -------- | ---------- | ---------------------------------------------------------------------- |
| 127.0.0.1           | overlay | held                 | 1       | OK         | 0.036s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | broken               | 1       | FAIL       | 0.038s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | broken               | 2       | FAIL       | 0.035s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | broken               | 3       | FAIL       | 0.032s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | raised               | 1       | FAIL       | 0.110s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | raised               | 2       | FAIL       | 0.105s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | raised               | 3       | FAIL       | 0.108s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | flaky                | 1       | FAIL       | 0.038s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | flaky                | 2       | OK         | 0.040s   | 1          | -                                                                      |
| 127.0.0.1           | overlay | noisy                | 1       | FAIL       | 0.995s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | noisy                | 2       | FAIL       | 0.994s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | noisy                | 3       | FAIL       | 1.015s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | browse               | 1       | OK         | 0.153s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | menu-left-open       | 1       | OK         | 0.043s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | menu-closed-again    | 1       | OK         | 0.042s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | leaves-things-behind | 1       | OK         | 0.113s   | 0          | -                                                                      |
| 127.0.0.1           | overlay | missing-file         | 1       | SKIP       | 0.000s   | 0          | missing /tmp/e2e-observability-fixture-cfpfp5j1/suites/missing_file.py |
| 127.0.0.1           | overlay | cut-short            | 1       | OK         | 0.039s   | 0          | -                                                                      |
| 127.0.0.1           | perf    | a-benchmark          | 1       | OK         | 0.038s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | held                 | 1       | OK         | 0.038s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | broken               | 1       | FAIL       | 0.035s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | broken               | 2       | FAIL       | 0.039s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | broken               | 3       | FAIL       | 0.035s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | raised               | 1       | FAIL       | 0.109s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | raised               | 2       | FAIL       | 0.118s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | raised               | 3       | FAIL       | 0.121s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | flaky                | 1       | FAIL       | 0.040s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | flaky                | 2       | OK         | 0.046s   | 1          | -                                                                      |
| 127.0.0.1@localhost | overlay | noisy                | 1       | FAIL       | 1.016s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | noisy                | 2       | FAIL       | 1.043s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | noisy                | 3       | FAIL       | 1.030s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | browse               | 1       | FAIL       | 0.706s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | browse               | 2       | FAIL       | 0.734s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | browse               | 3       | FAIL       | 0.706s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | menu-left-open       | 1       | OK         | 0.053s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | menu-closed-again    | 1       | OK         | 0.049s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | leaves-things-behind | 1       | OK         | 0.164s   | 0          | -                                                                      |
| 127.0.0.1@localhost | overlay | missing-file         | 1       | SKIP       | 0.000s   | 0          | missing /tmp/e2e-observability-fixture-cfpfp5j1/suites/missing_file.py |
| 127.0.0.1@localhost | overlay | cut-short            | 1       | incomplete | -        | 0          | -                                                                      |

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
| 127.0.0.1           | broken | 3      | needs the ftp-listing-full-length fix, which this machine does not have |
| 127.0.0.1@localhost | broken | 3      | needs the ftp-listing-full-length fix, which this machine does not have |

## What this run changed

What the action log says the run did to the device and, where a mutation has an undoing request, whether that request was made. A suite may leave something behind on purpose, so this is a list for a reader to judge rather than a verdict.

| Target              | Suite run                                          | What                                 | Where                                             |
| ------------------- | -------------------------------------------------- | ------------------------------------ | ------------------------------------------------- |
| 127.0.0.1           | 127.0.0.1/overlay/leaves-things-behind/1           | mount not undone                     | /v1/drives/a                                      |
| 127.0.0.1@localhost | 127.0.0.1@localhost/overlay/leaves-things-behind/1 | mount not undone                     | /v1/drives/a                                      |
| 127.0.0.1           | 127.0.0.1 (the runner itself)                      | written 2 times, last set to a value | /v1/configs/Network Settings/Log to Syslog Server |
| 127.0.0.1@localhost | 127.0.0.1@localhost/overlay/leaves-things-behind/1 | set to 192.168.1.2:5514              | /v1/configs/Network Settings/Log to Syslog Server |

## Retries

A suite that failed was run again. Every attempt is in the records and in the verdict table above; this is the same information collected in one place, because a run whose verdict is OK can still be a run in which something is intermittently broken.

| Suite run                            | Attempts | Verdicts                 | Outcome |
| ------------------------------------ | -------- | ------------------------ | ------- |
| 127.0.0.1/overlay/broken/3           | 3        | FAIL then FAIL then FAIL | FAIL    |
| 127.0.0.1/overlay/flaky/2            | 2        | FAIL then OK             | OK      |
| 127.0.0.1/overlay/noisy/3            | 3        | FAIL then FAIL then FAIL | FAIL    |
| 127.0.0.1/overlay/raised/3           | 3        | FAIL then FAIL then FAIL | FAIL    |
| 127.0.0.1@localhost/overlay/broken/3 | 3        | FAIL then FAIL then FAIL | FAIL    |
| 127.0.0.1@localhost/overlay/browse/3 | 3        | FAIL then FAIL then FAIL | FAIL    |
| 127.0.0.1@localhost/overlay/flaky/2  | 2        | FAIL then OK             | OK      |
| 127.0.0.1@localhost/overlay/noisy/3  | 3        | FAIL then FAIL then FAIL | FAIL    |
| 127.0.0.1@localhost/overlay/raised/3 | 3        | FAIL then FAIL then FAIL | FAIL    |

Checks that failed on one attempt and passed on another:

| Check                                 | Label              | Attempts |
| ------------------------------------- | ------------------ | -------- |
| 127.0.0.1/overlay/flaky/2/1           | the device is well | 2        |
| 127.0.0.1@localhost/overlay/flaky/2/1 | the device is well | 2        |

## Failing checks

### 127.0.0.1/overlay/broken/1/1 - the row survives a redraw

`FAIL` after 0.000s, at 2026-09-02 22:51:27, reported `0 rows, expected 20`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.
- First failure: no other check in this suite run failed before it.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/capture/overlay-broken-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/capture/overlay-broken-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s broken --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/broken.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1/overlay-broken.log`, which is attempt 1 of 3:

```
[01] the row survives a redraw ... FAIL (0 rows, expected 20, 0.000s)
[02] the name is listed in full ... SKIP (needs the ftp-listing-full-length fix, which this machine does not have, 0.000s)
```

### 127.0.0.1/overlay/broken/2/1 - the row survives a redraw

`FAIL` after 0.000s, at 2026-09-02 22:51:27, reported `0 rows, expected 20`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.
- First failure: no other check in this suite run failed before it.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/attempt-2/capture/overlay-broken-2-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/attempt-2/capture/overlay-broken-2-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s broken --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/broken.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1/attempt-2/overlay-broken.log`, which is attempt 2 of 3:

```
[01] the row survives a redraw ... FAIL (0 rows, expected 20, 0.000s)
[02] the name is listed in full ... SKIP (needs the ftp-listing-full-length fix, which this machine does not have, 0.000s)
```

### 127.0.0.1/overlay/broken/3/1 - the row survives a redraw

`FAIL` after 0.000s, at 2026-09-02 22:51:28, reported `0 rows, expected 20`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.
- First failure: no other check in this suite run failed before it.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/attempt-3/capture/overlay-broken-3-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/attempt-3/capture/overlay-broken-3-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s broken --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/broken.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1/attempt-3/overlay-broken.log`, which is attempt 3 of 3:

```
[01] the row survives a redraw ... FAIL (0 rows, expected 20, 0.000s)
[02] the name is listed in full ... SKIP (needs the ftp-listing-full-length fix, which this machine does not have, 0.000s)
```

### 127.0.0.1/overlay/flaky/1/1 - the device is well

`FAIL` after 0.000s, at 2026-09-02 22:51:28, reported `the listener is gone`.

- Passed on retry: this check passed on attempt 2 of 2.
- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.
- Passed elsewhere: the same check OK on 127.0.0.1@localhost.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/capture/overlay-flaky-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/capture/overlay-flaky-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s flaky --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/flaky.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1/overlay-flaky.log`, which is attempt 1 of 2:

```
[01] the device is well ... FAIL (the listener is gone, 0.000s)
```

### 127.0.0.1/overlay/noisy/1/1 - the drive answers

`FAIL` after 0.908s, at 2026-09-02 22:51:30, reported `the drive did not answer`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/capture/overlay-noisy-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/capture/overlay-noisy-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s noisy --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/noisy.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1/overlay-noisy.log`, which is attempt 1 of 3:

```
[01] the drive answers ... FAIL (the drive did not answer, 0.908s)
```

### 127.0.0.1/overlay/noisy/2/1 - the drive answers

`FAIL` after 0.908s, at 2026-09-02 22:51:31, reported `the drive did not answer`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/attempt-2/capture/overlay-noisy-2-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/attempt-2/capture/overlay-noisy-2-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s noisy --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/noisy.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1/attempt-2/overlay-noisy.log`, which is attempt 2 of 3:

```
[01] the drive answers ... FAIL (the drive did not answer, 0.908s)
```

### 127.0.0.1/overlay/noisy/3/1 - the drive answers

`FAIL` after 0.922s, at 2026-09-02 22:51:32, reported `the drive did not answer`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1@localhost.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/attempt-3/capture/overlay-noisy-3-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/attempt-3/capture/overlay-noisy-3-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s noisy --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/noisy.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1/attempt-3/overlay-noisy.log`, which is attempt 3 of 3:

```
[01] the drive answers ... FAIL (the drive did not answer, 0.922s)
```

### 127.0.0.1@localhost/overlay/broken/1/1 - the row survives a redraw

`FAIL` after 0.000s, at 2026-09-02 22:51:33, reported `0 rows, expected 20`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1.
- First failure: no other check in this suite run failed before it.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/capture/overlay-broken-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-broken-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s broken --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/broken.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1-at-localhost/overlay-broken.log`, which is attempt 1 of 3:

```
[01] the row survives a redraw ... FAIL (0 rows, expected 20, 0.000s)
[02] the name is listed in full ... SKIP (needs the ftp-listing-full-length fix, which this machine does not have, 0.000s)
```

### 127.0.0.1@localhost/overlay/broken/2/1 - the row survives a redraw

`FAIL` after 0.000s, at 2026-09-02 22:51:33, reported `0 rows, expected 20`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1.
- First failure: no other check in this suite run failed before it.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/attempt-2/capture/overlay-broken-2-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/attempt-2/capture/overlay-broken-2-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s broken --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/broken.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1-at-localhost/attempt-2/overlay-broken.log`, which is attempt 2 of 3:

```
[01] the row survives a redraw ... FAIL (0 rows, expected 20, 0.000s)
[02] the name is listed in full ... SKIP (needs the ftp-listing-full-length fix, which this machine does not have, 0.000s)
```

### 127.0.0.1@localhost/overlay/broken/3/1 - the row survives a redraw

`FAIL` after 0.000s, at 2026-09-02 22:51:33, reported `0 rows, expected 20`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1.
- First failure: no other check in this suite run failed before it.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/attempt-3/capture/overlay-broken-3-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/attempt-3/capture/overlay-broken-3-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s broken --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/broken.py`, which carries the check's label as a literal string.

Last 2 line(s) of `127.0.0.1-at-localhost/attempt-3/overlay-broken.log`, which is attempt 3 of 3:

```
[01] the row survives a redraw ... FAIL (0 rows, expected 20, 0.000s)
[02] the name is listed in full ... SKIP (needs the ftp-listing-full-length fix, which this machine does not have, 0.000s)
```

### 127.0.0.1@localhost/overlay/flaky/1/1 - the device is well

`FAIL` after 0.000s, at 2026-09-02 22:51:34, reported `the listener is gone`.

- Passed on retry: this check passed on attempt 2 of 2.
- Failed elsewhere: the same check FAIL on 127.0.0.1.
- Passed elsewhere: the same check OK on 127.0.0.1.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/capture/overlay-flaky-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-flaky-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s flaky --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/flaky.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1-at-localhost/overlay-flaky.log`, which is attempt 1 of 2:

```
[01] the device is well ... FAIL (the listener is gone, 0.000s)
```

### 127.0.0.1@localhost/overlay/noisy/1/1 - the drive answers

`FAIL` after 0.916s, at 2026-09-02 22:51:35, reported `the drive did not answer`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/capture/overlay-noisy-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-noisy-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s noisy --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/noisy.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1-at-localhost/overlay-noisy.log`, which is attempt 1 of 3:

```
[01] the drive answers ... FAIL (the drive did not answer, 0.916s)
```

### 127.0.0.1@localhost/overlay/noisy/2/1 - the drive answers

`FAIL` after 0.933s, at 2026-09-02 22:51:36, reported `the drive did not answer`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/attempt-2/capture/overlay-noisy-2-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/attempt-2/capture/overlay-noisy-2-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s noisy --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/noisy.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1-at-localhost/attempt-2/overlay-noisy.log`, which is attempt 2 of 3:

```
[01] the drive answers ... FAIL (the drive did not answer, 0.933s)
```

### 127.0.0.1@localhost/overlay/noisy/3/1 - the drive answers

`FAIL` after 0.914s, at 2026-09-02 22:51:38, reported `the drive did not answer`.

- Repeated: this check failed on 3 of the 3 attempts this suite was given.
- Failed elsewhere: the same check FAIL on 127.0.0.1.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/attempt-3/capture/overlay-noisy-3-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/attempt-3/capture/overlay-noisy-3-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s noisy --mode overlay`

Source: `/tmp/e2e-observability-fixture-cfpfp5j1/suites/noisy.py`, which carries the check's label as a literal string.

Last 1 line(s) of `127.0.0.1-at-localhost/attempt-3/overlay-noisy.log`, which is attempt 3 of 3:

```
[01] the drive answers ... FAIL (the drive did not answer, 0.914s)
```

### 127.0.0.1/overlay/raised/1 - the suite itself

`FAIL`, with no failing check of its own.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/capture/overlay-raised-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/capture/overlay-raised-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s raised --mode overlay`

Last 4 line(s) of `127.0.0.1/overlay-raised.log`, which is attempt 1 of 3:

```
[01] the device answers ... Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/raised.py", line 18, in <module>
    raise RuntimeError('the device stopped answering mid-check')
RuntimeError: the device stopped answering mid-check
```

### 127.0.0.1/overlay/raised/2 - the suite itself

`FAIL`, with no failing check of its own.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/attempt-2/capture/overlay-raised-2-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/attempt-2/capture/overlay-raised-2-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s raised --mode overlay`

Last 4 line(s) of `127.0.0.1/attempt-2/overlay-raised.log`, which is attempt 2 of 3:

```
[01] the device answers ... Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/raised.py", line 18, in <module>
    raise RuntimeError('the device stopped answering mid-check')
RuntimeError: the device stopped answering mid-check
```

### 127.0.0.1/overlay/raised/3 - the suite itself

`FAIL`, with no failing check of its own.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1/attempt-3/capture/overlay-raised-3-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives with nothing mounted. Everything the capture read is in `127.0.0.1/attempt-3/capture/overlay-raised-3-state.json`.

Reproduce: `./run-tests -H 127.0.0.1 -s raised --mode overlay`

Last 4 line(s) of `127.0.0.1/attempt-3/overlay-raised.log`, which is attempt 3 of 3:

```
[01] the device answers ... Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/raised.py", line 18, in <module>
    raise RuntimeError('the device stopped answering mid-check')
RuntimeError: the device stopped answering mid-check
```

### 127.0.0.1@localhost/overlay/raised/1 - the suite itself

`FAIL`, with no failing check of its own.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/capture/overlay-raised-1-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/capture/overlay-raised-1-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s raised --mode overlay`

Last 4 line(s) of `127.0.0.1-at-localhost/overlay-raised.log`, which is attempt 1 of 3:

```
[01] the device answers ... Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/raised.py", line 18, in <module>
    raise RuntimeError('the device stopped answering mid-check')
RuntimeError: the device stopped answering mid-check
```

### 127.0.0.1@localhost/overlay/raised/2 - the suite itself

`FAIL`, with no failing check of its own.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/attempt-2/capture/overlay-raised-2-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/attempt-2/capture/overlay-raised-2-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s raised --mode overlay`

Last 4 line(s) of `127.0.0.1-at-localhost/attempt-2/overlay-raised.log`, which is attempt 2 of 3:

```
[01] the device answers ... Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/raised.py", line 18, in <module>
    raise RuntimeError('the device stopped answering mid-check')
RuntimeError: the device stopped answering mid-check
```

### 127.0.0.1@localhost/overlay/raised/3 - the suite itself

`FAIL`, with no failing check of its own.

The C64's own screen, read from $0400 and decoded as screen codes, which is best effort because the matrix moves with the VIC bank and with $D018, when this suite ended (`127.0.0.1-at-localhost/attempt-3/capture/overlay-raised-3-screen.txt`):

```
READY.

```

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/attempt-3/capture/overlay-raised-3-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s raised --mode overlay`

Last 4 line(s) of `127.0.0.1-at-localhost/attempt-3/overlay-raised.log`, which is attempt 3 of 3:

```
[01] the device answers ... Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/raised.py", line 18, in <module>
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

Last 12 line(s) of `127.0.0.1-at-localhost/overlay-browse.log`, which is attempt 1 of 3:

```
Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/browse.py", line 20, in <module>
    backend = ui_backend.make_backend('overlay', ARGS.host,
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 2208, in make_backend
    return RestBackend(host, password, timeout, interface_type=_MODE_INTERFACE_TYPE[mode])
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 1113, in __init__
    close_host_menu(self.input_host, password, timeout)
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 177, in close_host_menu
    raise Failure(
report.Failure: the menu on localhost would not close, so keys sent to it would be consumed by that menu instead of reaching the cartridge
```

### 127.0.0.1@localhost/overlay/browse/2 - the suite itself

`FAIL`, with no failing check of its own.

The menu screen when this suite ended (`127.0.0.1-at-localhost/attempt-2/capture/overlay-browse-2-screen.txt`):

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

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/attempt-2/capture/overlay-browse-2-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s browse --mode overlay`

Last 12 line(s) of `127.0.0.1-at-localhost/attempt-2/overlay-browse.log`, which is attempt 2 of 3:

```
Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/browse.py", line 20, in <module>
    backend = ui_backend.make_backend('overlay', ARGS.host,
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 2208, in make_backend
    return RestBackend(host, password, timeout, interface_type=_MODE_INTERFACE_TYPE[mode])
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 1113, in __init__
    close_host_menu(self.input_host, password, timeout)
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 177, in close_host_menu
    raise Failure(
report.Failure: the menu on localhost would not close, so keys sent to it would be consumed by that menu instead of reaching the cartridge
```

### 127.0.0.1@localhost/overlay/browse/3 - the suite itself

`FAIL`, with no failing check of its own.

The menu screen when this suite ended (`127.0.0.1-at-localhost/attempt-3/capture/overlay-browse-3-screen.txt`):

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

Device state: free heap 1500000 B, low-water 1200000 B of 2000000 B; drives a: /Usb0/game.d64. Everything the capture read is in `127.0.0.1-at-localhost/attempt-3/capture/overlay-browse-3-state.json`.

Reproduce: `./run-tests -H 127.0.0.1@localhost -s browse --mode overlay`

Last 12 line(s) of `127.0.0.1-at-localhost/attempt-3/overlay-browse.log`, which is attempt 3 of 3:

```
Traceback (most recent call last):
  File "/tmp/e2e-observability-fixture-cfpfp5j1/suites/browse.py", line 20, in <module>
    backend = ui_backend.make_backend('overlay', ARGS.host,
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 2208, in make_backend
    return RestBackend(host, password, timeout, interface_type=_MODE_INTERFACE_TYPE[mode])
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 1113, in __init__
    close_host_menu(self.input_host, password, timeout)
  File "/home/chris/dev/c64/1541ultimate/tests/e2e/lib/ui_backend.py", line 177, in close_host_menu
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
| held                                  | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| broken                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| broken: after failure,                | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| broken                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 2ms   |
| broken: after failure,                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| broken                                | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 1ms   |
| broken: after the last attempt,       | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 2ms   |
| raised                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 2ms   |
| raised: after failure,                | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| raised                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised: after failure,                | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised                                | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised: after the last attempt,       | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| flaky                                 | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 1ms   |
| flaky: after failure,                 | DEGRADED | 2ms  | 1ms  | FAIL | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| flaky: after failure, after recovery, | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| flaky                                 | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 2ms   |
| noisy                                 | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| noisy: after failure,                 | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| noisy                                 | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| noisy: after failure,                 | OK       | 2ms  | 0ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| noisy                                 | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| noisy: after the last attempt,        | OK       | 4ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 6ms   |
| browse                                | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| menu-left-open                        | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| menu-closed-again                     | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| leaves-things-behind                  | OK       | 2ms  | 1ms  | 42ms | 0ms    | 5ms   | 0ms | 1500000B | 4ms    | 6ms   |
| cut-short                             | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |

### 127.0.0.1@localhost

| Sweep                                 | Verdict  | ping | rest | ftp  | telnet | ident | dma | heap     | raster | jiffy |
| ------------------------------------- | -------- | ---- | ---- | ---- | ------ | ----- | --- | -------- | ------ | ----- |
| held                                  | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| broken                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| broken: after failure,                | OK       | 4ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| broken                                | OK       | 4ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| broken: after failure,                | OK       | 8ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 2ms   |
| broken                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 2ms   |
| broken: after the last attempt,       | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| raised                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 2ms   |
| raised: after failure,                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 2ms   |
| raised                                | OK       | 4ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised: after failure,                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 1ms   |
| raised                                | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| raised: after the last attempt,       | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| flaky                                 | OK       | 2ms  | 1ms  | 43ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| flaky: after failure,                 | DEGRADED | 2ms  | 1ms  | FAIL | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| flaky: after failure, after recovery, | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 1ms   |
| flaky                                 | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| noisy                                 | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| noisy: after failure,                 | OK       | 2ms  | 1ms  | 41ms | 1ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| noisy                                 | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 1ms | 1500000B | 2ms    | 3ms   |
| noisy: after failure,                 | OK       | 2ms  | 1ms  | 42ms | 2ms    | 1ms   | 0ms | 1500000B | 5ms    | 2ms   |
| noisy                                 | OK       | 2ms  | 1ms  | 42ms | 0ms    | 2ms   | 0ms | 1500000B | 4ms    | 2ms   |
| noisy: after the last attempt,        | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 1ms    | 1ms   |
| browse                                | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| browse: after failure,                | OK       | 4ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| browse                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| browse: after failure,                | OK       | 4ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| browse                                | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| browse: after the last attempt,       | OK       | 2ms  | 1ms  | 42ms | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| menu-left-open                        | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| menu-closed-again                     | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | skip   | skip  |
| leaves-things-behind                  | OK       | 2ms  | 1ms  | 43ms | 1ms    | 1ms   | 0ms | 1500000B | 2ms    | 2ms   |
| cut-short                             | OK       | 2ms  | 1ms  | 41ms | 0ms    | 1ms   | 0ms | 1500000B | 2ms    | 1ms   |

## Files in this run

A capture file's name is its suite run's key with `/` written `-` and the target dropped, because the file already sits under that target's directory.

Reaching one of these from a build page is a download and an unzip: GitHub serves no URL for a single file inside a zipped artifact, so the artifact link on the build page is the last click there is.

| Path                                                                   | Bytes  | What it is                                                                                                                             |
| ---------------------------------------------------------------------- | ------ | -------------------------------------------------------------------------------------------------------------------------------------- |
| `index.md`                                                             | -      | this report, written by tools/e2e_report.py                                                                                            |
| `run.jsonl`                                                            | 5963   | the run's own records: the plan, the health sweeps, the suite verdicts and the run result, written by run-tests                        |
| `run.log`                                                              | 34839  | run-tests' own console output                                                                                                          |
| `127.0.0.1/interactions.jsonl`                                         | 117227 | every interaction the harness had with this device: each REST request and its answer, each Telnet exchange, each FTP command and reply |
| `127.0.0.1/interactions.seq`                                           | 4      | the next sequence number for `interactions.jsonl`, and the lock every process writing it takes                                         |
| `127.0.0.1/overlay-broken.jsonl`                                       | 1579   | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-broken.log`                                         | 193    | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-browse.jsonl`                                       | 403    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-browse.log`                                         | 47     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-cut-short.jsonl`                                    | 416    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-cut-short.log`                                      | 73     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-flaky.jsonl`                                        | 449    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-flaky.log`                                          | 64     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-held.jsonl`                                         | 465    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-held.log`                                           | 126    | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-leaves-things-behind.jsonl`                         | 983    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-leaves-things-behind.log`                           | 77     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-menu-closed-again.jsonl`                            | 217    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-menu-closed-again.log`                              | 37     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-menu-left-open.jsonl`                               | 225    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-menu-left-open.log`                                 | 47     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-noisy.jsonl`                                        | 707    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-noisy.log`                                          | 67     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/overlay-raised.jsonl`                                       | 0      | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/overlay-raised.log`                                         | 269    | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/perf-a-benchmark.jsonl`                                     | 244    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1/perf-a-benchmark.log`                                       | 72     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1/run.jsonl`                                                  | 41133  | the run's own records: the plan, the health sweeps, the suite verdicts and the run result, written by run-tests                        |
| `127.0.0.1/run.log`                                                    | 9313   | run-tests' own console output                                                                                                          |
| `127.0.0.1/screens.jsonl`                                              | 10504  | every distinct screen the harness read, as text and as raw bytes                                                                       |
| `127.0.0.1/syslog.txt`                                                 | 28068  | the device's own log, as the collector received it, best effort and incomplete by construction                                         |
| `127.0.0.1/transcript.txt`                                             | 39938  | the same interactions as one line each, sharing their sequence numbers with `interactions.jsonl`                                       |
| `127.0.0.1/attempt-2/overlay-broken.log`                               | 193    | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1/attempt-2/overlay-flaky.log`                                | 51     | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1/attempt-2/overlay-noisy.log`                                | 67     | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1/attempt-2/overlay-raised.log`                               | 269    | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1/attempt-2/capture/overlay-broken-2-screen.bin`              | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/attempt-2/capture/overlay-broken-2-screen.txt`              | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/attempt-2/capture/overlay-broken-2-state.json`              | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/attempt-2/capture/overlay-noisy-2-screen.bin`               | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/attempt-2/capture/overlay-noisy-2-screen.txt`               | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/attempt-2/capture/overlay-noisy-2-state.json`               | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/attempt-2/capture/overlay-raised-2-screen.bin`              | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/attempt-2/capture/overlay-raised-2-screen.txt`              | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/attempt-2/capture/overlay-raised-2-state.json`              | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/attempt-3/overlay-broken.log`                               | 193    | that suite run's console output on attempt 3, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1/attempt-3/overlay-noisy.log`                                | 67     | that suite run's console output on attempt 3, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1/attempt-3/overlay-raised.log`                               | 269    | that suite run's console output on attempt 3, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1/attempt-3/capture/overlay-broken-3-screen.bin`              | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/attempt-3/capture/overlay-broken-3-screen.txt`              | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/attempt-3/capture/overlay-broken-3-state.json`              | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/attempt-3/capture/overlay-noisy-3-screen.bin`               | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/attempt-3/capture/overlay-noisy-3-screen.txt`               | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/attempt-3/capture/overlay-noisy-3-state.json`               | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/attempt-3/capture/overlay-raised-3-screen.bin`              | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/attempt-3/capture/overlay-raised-3-screen.txt`              | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/attempt-3/capture/overlay-raised-3-state.json`              | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/bodies/28aa6db3cd54d3d9.bin`                                | 1000   | one response body, kept once and referred to by its digest                                                                             |
| `127.0.0.1/bodies/80b3340f20b9a5e8.bin`                                | 400    | one response body, kept once and referred to by its digest                                                                             |
| `127.0.0.1/bodies/9a83f2d08f89db8c.bin`                                | 2000   | one response body, kept once and referred to by its digest                                                                             |
| `127.0.0.1/bodies/a58ada436a91a558.bin`                                | 239    | one response body, kept once and referred to by its digest                                                                             |
| `127.0.0.1/capture/overlay-broken-1-screen.bin`                        | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/capture/overlay-broken-1-screen.txt`                        | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/capture/overlay-broken-1-state.json`                        | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/capture/overlay-flaky-1-screen.bin`                         | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/capture/overlay-flaky-1-screen.txt`                         | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/capture/overlay-flaky-1-state.json`                         | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/capture/overlay-noisy-1-screen.bin`                         | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/capture/overlay-noisy-1-screen.txt`                         | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/capture/overlay-noisy-1-state.json`                         | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1/capture/overlay-raised-1-screen.bin`                        | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1/capture/overlay-raised-1-screen.txt`                        | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1/capture/overlay-raised-1-state.json`                        | 547    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/interactions.jsonl`                            | 120420 | every interaction the harness had with this device: each REST request and its answer, each Telnet exchange, each FTP command and reply |
| `127.0.0.1-at-localhost/interactions.seq`                              | 4      | the next sequence number for `interactions.jsonl`, and the lock every process writing it takes                                         |
| `127.0.0.1-at-localhost/overlay-broken.jsonl`                          | 1639   | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-broken.log`                            | 193    | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-browse.jsonl`                          | 573    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-browse.log`                            | 931    | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-cut-short.jsonl`                       | 252    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-cut-short.log`                         | 36     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-flaky.jsonl`                           | 470    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-flaky.log`                             | 64     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-held.jsonl`                            | 483    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-held.log`                              | 126    | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-leaves-things-behind.jsonl`            | 1022   | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-leaves-things-behind.log`              | 77     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-menu-closed-again.jsonl`               | 226    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-menu-closed-again.log`                 | 37     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-menu-left-open.jsonl`                  | 235    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-menu-left-open.log`                    | 47     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-noisy.jsonl`                           | 736    | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-noisy.log`                             | 67     | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/overlay-raised.jsonl`                          | 0      | one suite run's checks, scenarios and device actions                                                                                   |
| `127.0.0.1-at-localhost/overlay-raised.log`                            | 269    | that suite run's console output, stderr merged in, ANSI stripped                                                                       |
| `127.0.0.1-at-localhost/run.jsonl`                                     | 43674  | the run's own records: the plan, the health sweeps, the suite verdicts and the run result, written by run-tests                        |
| `127.0.0.1-at-localhost/run.log`                                       | 9274   | run-tests' own console output                                                                                                          |
| `127.0.0.1-at-localhost/screens.jsonl`                                 | 0      | every distinct screen the harness read, as text and as raw bytes                                                                       |
| `127.0.0.1-at-localhost/transcript.txt`                                | 39713  | the same interactions as one line each, sharing their sequence numbers with `interactions.jsonl`                                       |
| `127.0.0.1-at-localhost/attempt-2/overlay-broken.log`                  | 193    | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-2/overlay-browse.log`                  | 931    | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-2/overlay-flaky.log`                   | 51     | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-2/overlay-noisy.log`                   | 67     | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-2/overlay-raised.log`                  | 269    | that suite run's console output on attempt 2, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-broken-2-screen.bin` | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-broken-2-screen.txt` | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-broken-2-state.json` | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-browse-2-screen.bin` | 2000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-browse-2-screen.txt` | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-browse-2-state.json` | 565    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-noisy-2-screen.bin`  | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-noisy-2-screen.txt`  | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-noisy-2-state.json`  | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-raised-2-screen.bin` | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-raised-2-screen.txt` | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/attempt-2/capture/overlay-raised-2-state.json` | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/attempt-3/overlay-broken.log`                  | 193    | that suite run's console output on attempt 3, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-3/overlay-browse.log`                  | 931    | that suite run's console output on attempt 3, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-3/overlay-noisy.log`                   | 67     | that suite run's console output on attempt 3, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-3/overlay-raised.log`                  | 269    | that suite run's console output on attempt 3, stderr merged in, ANSI stripped                                                          |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-broken-3-screen.bin` | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-broken-3-screen.txt` | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-broken-3-state.json` | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-browse-3-screen.bin` | 2000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-browse-3-screen.txt` | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-browse-3-state.json` | 565    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-noisy-3-screen.bin`  | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-noisy-3-screen.txt`  | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-noisy-3-state.json`  | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-raised-3-screen.bin` | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-raised-3-screen.txt` | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/attempt-3/capture/overlay-raised-3-state.json` | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/bodies/28aa6db3cd54d3d9.bin`                   | 1000   | one response body, kept once and referred to by its digest                                                                             |
| `127.0.0.1-at-localhost/bodies/7f37356f699c1388.bin`                   | 2000   | one response body, kept once and referred to by its digest                                                                             |
| `127.0.0.1-at-localhost/bodies/c5e1848fc95d9d95.bin`                   | 253    | one response body, kept once and referred to by its digest                                                                             |
| `127.0.0.1-at-localhost/capture/overlay-broken-1-screen.bin`           | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/capture/overlay-broken-1-screen.txt`           | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/capture/overlay-broken-1-state.json`           | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-browse-1-screen.bin`           | 2000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/capture/overlay-browse-1-screen.txt`           | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/capture/overlay-browse-1-state.json`           | 565    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-flaky-1-screen.bin`            | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/capture/overlay-flaky-1-screen.txt`            | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/capture/overlay-flaky-1-state.json`            | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-noisy-1-screen.bin`            | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/capture/overlay-noisy-1-screen.txt`            | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/capture/overlay-noisy-1-state.json`            | 561    | the drive state and free heap when a suite failed                                                                                      |
| `127.0.0.1-at-localhost/capture/overlay-raised-1-screen.bin`           | 1000   | the same screen, as the device's own bytes                                                                                             |
| `127.0.0.1-at-localhost/capture/overlay-raised-1-screen.txt`           | 1025   | the screen a failing suite left, as text                                                                                               |
| `127.0.0.1-at-localhost/capture/overlay-raised-1-state.json`           | 561    | the drive state and free heap when a suite failed                                                                                      |

<!-- detail -->

## Timeline

Each line opens with the wall-clock time on the host that ran the gate, then the offset from the start of the run.

23:51:24 +00:00  the run warned: device log: localhost and 127.0.0.1 are both 127.0.0.1, so a datagram from it is attributed to 127.0.0.1 unless the port it arrived on says otherwise
23:51:24 +00:00  the run warned: device log: localhost resolves only to addresses another machine already claims and shares its syslog port, so its lines cannot be attributed and land in syslog-unknown-sender.txt
23:51:27 +00:03  the run warned: device log: 127.0.0.1 sent nothing when this run asked it for /v1/version, so its log is not reaching the collector on UDP 47716
23:51:27 +00:03  the run warned: device log: localhost sent nothing when this run asked it for /v1/version, so its log is not reaching the collector on UDP 47716
23:51:27 +00:03  127.0.0.1 warning: device log: this device is not configured to send its log anywhere (at the start of the run)
23:51:27 +00:03  127.0.0.1 GET /v1/machine:menu_screen -> 404: `Menu screen unavailable.`  (this request is made 38 times in this run and is shown twice)
23:51:27 +00:03  127.0.0.1 sweep held: OK
23:51:27 +00:03  127.0.0.1/overlay/held/1 started
23:51:27 +00:03  127.0.0.1/overlay/held/1 OK
23:51:27 +00:03  127.0.0.1 GET /v1/machine:menu_screen -> 404: `Menu screen unavailable.`  (this request is made 38 times in this run and is shown twice)
23:51:27 +00:03  127.0.0.1 sweep broken: OK
23:51:27 +00:03  127.0.0.1/overlay/broken/1 started
23:51:27 +00:03  127.0.0.1/overlay/broken/1/1 FAIL the row survives a redraw
23:51:27 +00:03  127.0.0.1/overlay/broken/1 FAIL
23:51:27 +00:03  127.0.0.1/overlay/broken/1 device state captured
23:51:27 +00:03  127.0.0.1 sweep broken: after failure,: OK
23:51:27 +00:03  127.0.0.1 sweep broken: OK
23:51:27 +00:03  127.0.0.1/overlay/broken/2 started
23:51:27 +00:03  127.0.0.1/overlay/broken/2/1 FAIL the row survives a redraw
23:51:27 +00:03  127.0.0.1/overlay/broken/2 FAIL
23:51:27 +00:03  127.0.0.1/overlay/broken/2 device state captured
23:51:27 +00:03  127.0.0.1 sweep broken: after failure,: OK
23:51:28 +00:03  127.0.0.1 sweep broken: OK
23:51:28 +00:03  127.0.0.1/overlay/broken/3 started
23:51:28 +00:03  127.0.0.1/overlay/broken/3/1 FAIL the row survives a redraw
23:51:28 +00:03  127.0.0.1/overlay/broken/3 FAIL
23:51:28 +00:03  127.0.0.1/overlay/broken/3 device state captured
23:51:28 +00:03  127.0.0.1 sweep broken: after the last attempt,: OK
23:51:28 +00:03  127.0.0.1 sweep raised: OK
23:51:28 +00:03  127.0.0.1/overlay/raised/1 started
23:51:28 +00:03  127.0.0.1/overlay/raised/1 FAIL
23:51:28 +00:03  127.0.0.1/overlay/raised/1 device state captured
23:51:28 +00:03  127.0.0.1 sweep raised: after failure,: OK
23:51:28 +00:03  127.0.0.1 sweep raised: OK
23:51:28 +00:03  127.0.0.1/overlay/raised/2 started
23:51:28 +00:04  127.0.0.1/overlay/raised/2 FAIL
23:51:28 +00:04  127.0.0.1/overlay/raised/2 device state captured
23:51:28 +00:04  127.0.0.1 sweep raised: after failure,: OK
23:51:28 +00:04  127.0.0.1 sweep raised: OK
23:51:28 +00:04  127.0.0.1/overlay/raised/3 started
23:51:28 +00:04  127.0.0.1/overlay/raised/3 FAIL
23:51:28 +00:04  127.0.0.1/overlay/raised/3 device state captured
23:51:28 +00:04  127.0.0.1 sweep raised: after the last attempt,: OK
23:51:28 +00:04  127.0.0.1 sweep flaky: OK
23:51:28 +00:04  127.0.0.1/overlay/flaky/1 started
23:51:28 +00:04  127.0.0.1/overlay/flaky/1/1 FAIL the device is well
23:51:28 +00:04  127.0.0.1/overlay/flaky/1 FAIL
23:51:28 +00:04  127.0.0.1/overlay/flaky/1 device state captured
23:51:28 +00:04  127.0.0.1 sweep flaky: after failure,: DEGRADED
23:51:28 +00:04  127.0.0.1 sweep flaky: after failure, after recovery,: OK
23:51:28 +00:04  127.0.0.1 sweep flaky: OK
23:51:28 +00:04  127.0.0.1/overlay/flaky/2 started
23:51:28 +00:04  127.0.0.1 was recovered 1 time(s) around flaky
23:51:28 +00:04  127.0.0.1/overlay/flaky/2 OK
23:51:29 +00:04  127.0.0.1 sweep noisy: OK
23:51:29 +00:04  127.0.0.1/overlay/noisy/1 started
23:51:29 +00:04  127.0.0.1 restarted, seen in its own log
23:51:30 +00:05  127.0.0.1/overlay/noisy/1/1 FAIL the drive answers
23:51:30 +00:05  127.0.0.1/overlay/noisy/1 FAIL
23:51:30 +00:05  127.0.0.1/overlay/noisy/1 device state captured
23:51:30 +00:05  127.0.0.1 sweep noisy: after failure,: OK
23:51:30 +00:05  127.0.0.1 sweep noisy: OK
23:51:30 +00:05  127.0.0.1/overlay/noisy/2 started
23:51:30 +00:05  127.0.0.1 restarted, seen in its own log
23:51:31 +00:06  127.0.0.1/overlay/noisy/2/1 FAIL the drive answers
23:51:31 +00:06  127.0.0.1/overlay/noisy/2 FAIL
23:51:31 +00:06  127.0.0.1/overlay/noisy/2 device state captured
23:51:31 +00:06  127.0.0.1 sweep noisy: after failure,: OK
23:51:31 +00:06  127.0.0.1 sweep noisy: OK
23:51:31 +00:06  127.0.0.1/overlay/noisy/3 started
23:51:31 +00:06  127.0.0.1 restarted, seen in its own log
23:51:32 +00:07  127.0.0.1/overlay/noisy/3/1 FAIL the drive answers
23:51:32 +00:07  127.0.0.1/overlay/noisy/3 FAIL
23:51:32 +00:07  127.0.0.1/overlay/noisy/3 device state captured
23:51:32 +00:07  127.0.0.1 sweep noisy: after the last attempt,: OK
23:51:32 +00:07  127.0.0.1 sweep browse: OK
23:51:32 +00:07  127.0.0.1/overlay/browse/1 started
23:51:32 +00:08  127.0.0.1/overlay/browse/1 POST /v1/machine:input
23:51:32 +00:08  127.0.0.1/overlay/browse/1 OK
23:51:32 +00:08  127.0.0.1 sweep menu-left-open: OK
23:51:32 +00:08  127.0.0.1/overlay/menu-left-open/1 started
23:51:32 +00:08  127.0.0.1/overlay/menu-left-open/1 OK
23:51:32 +00:08  127.0.0.1 sweep menu-closed-again: OK
23:51:32 +00:08  127.0.0.1/overlay/menu-closed-again/1 started
23:51:32 +00:08  127.0.0.1/overlay/menu-closed-again/1 OK
23:51:32 +00:08  127.0.0.1 sweep leaves-things-behind: OK
23:51:32 +00:08  127.0.0.1/overlay/leaves-things-behind/1 started
23:51:32 +00:08  127.0.0.1/overlay/leaves-things-behind/1 PUT /v1/drives/a:mount {"image": "/Usb0/game.d64"}
23:51:32 +00:08  127.0.0.1/overlay/leaves-things-behind/1 PUT /v1/configs/Network%20Settings/Log%20to%20Syslog%20Server {"value": "192.168.1.2:5514"}
23:51:32 +00:08  127.0.0.1/overlay/leaves-things-behind/1 OK
23:51:32 +00:08  127.0.0.1/overlay/missing-file/1 started
23:51:32 +00:08  127.0.0.1/overlay/missing-file/1 SKIP: missing /tmp/e2e-observability-fixture-cfpfp5j1/suites/missing_file.py
23:51:32 +00:08  127.0.0.1 sweep cut-short: OK
23:51:32 +00:08  127.0.0.1/overlay/cut-short/1 started
23:51:32 +00:08  127.0.0.1/overlay/cut-short/1 OK
23:51:32 +00:08  5 device requests (GET, POST, PUT)
23:51:32 +00:08  127.0.0.1/perf/a-benchmark/1 started
23:51:33 +00:08  127.0.0.1/perf/a-benchmark/1 OK
23:51:33 +00:08  127.0.0.1 warning: device log: this device sends its log to port 5514 and this run collects on 47716, so none of it will arrive; set 'Log to Syslog Server' to '192.168.1.2:47716' and reboot the device (at the end of the run)
23:51:33 +00:08  127.0.0.1 PUT /v1/configs/Network%20Settings/Log%20to%20Syslog%20Server {"value": ""}
23:51:33 +00:08  127.0.0.1@localhost warning: device log: this device is not configured to send its log anywhere (at the start of the run)
23:51:33 +00:08  4 device requests (GET)
23:51:33 +00:08  127.0.0.1@localhost sweep held: OK
23:51:33 +00:08  127.0.0.1@localhost/overlay/held/1 started
23:51:33 +00:08  127.0.0.1@localhost/overlay/held/1 OK
23:51:33 +00:08  127.0.0.1@localhost GET /v1/machine:menu_screen -> 404: `Menu screen unavailable.`  (this request is made 36 times in this run and is shown twice)
23:51:33 +00:08  127.0.0.1@localhost sweep broken: OK
23:51:33 +00:08  127.0.0.1@localhost/overlay/broken/1 started
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/1/1 FAIL the row survives a redraw
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/1 FAIL
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/1 device state captured
23:51:33 +00:09  127.0.0.1@localhost GET /v1/machine:menu_screen -> 404: `Menu screen unavailable.`  (this request is made 36 times in this run and is shown twice)
23:51:33 +00:09  127.0.0.1@localhost sweep broken: after failure,: OK
23:51:33 +00:09  127.0.0.1@localhost sweep broken: OK
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/2 started
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/2/1 FAIL the row survives a redraw
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/2 FAIL
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/2 device state captured
23:51:33 +00:09  127.0.0.1@localhost sweep broken: after failure,: OK
23:51:33 +00:09  127.0.0.1@localhost sweep broken: OK
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/3 started
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/3/1 FAIL the row survives a redraw
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/3 FAIL
23:51:33 +00:09  127.0.0.1@localhost/overlay/broken/3 device state captured
23:51:33 +00:09  127.0.0.1@localhost sweep broken: after the last attempt,: OK
23:51:33 +00:09  127.0.0.1@localhost sweep raised: OK
23:51:33 +00:09  127.0.0.1@localhost/overlay/raised/1 started
23:51:33 +00:09  127.0.0.1@localhost/overlay/raised/1 FAIL
23:51:33 +00:09  127.0.0.1@localhost/overlay/raised/1 device state captured
23:51:34 +00:09  127.0.0.1@localhost sweep raised: after failure,: OK
23:51:34 +00:09  127.0.0.1@localhost sweep raised: OK
23:51:34 +00:09  127.0.0.1@localhost/overlay/raised/2 started
23:51:34 +00:09  127.0.0.1@localhost/overlay/raised/2 FAIL
23:51:34 +00:09  127.0.0.1@localhost/overlay/raised/2 device state captured
23:51:34 +00:09  127.0.0.1@localhost sweep raised: after failure,: OK
23:51:34 +00:09  127.0.0.1@localhost sweep raised: OK
23:51:34 +00:09  127.0.0.1@localhost/overlay/raised/3 started
23:51:34 +00:09  127.0.0.1@localhost/overlay/raised/3 FAIL
23:51:34 +00:09  127.0.0.1@localhost/overlay/raised/3 device state captured
23:51:34 +00:10  127.0.0.1@localhost sweep raised: after the last attempt,: OK
23:51:34 +00:10  127.0.0.1@localhost sweep flaky: OK
23:51:34 +00:10  127.0.0.1@localhost/overlay/flaky/1 started
23:51:34 +00:10  127.0.0.1@localhost/overlay/flaky/1/1 FAIL the device is well
23:51:34 +00:10  127.0.0.1@localhost/overlay/flaky/1 FAIL
23:51:34 +00:10  127.0.0.1@localhost/overlay/flaky/1 device state captured
23:51:34 +00:10  127.0.0.1@localhost sweep flaky: after failure,: DEGRADED
23:51:34 +00:10  127.0.0.1@localhost sweep flaky: after failure, after recovery,: OK
23:51:34 +00:10  127.0.0.1@localhost sweep flaky: OK
23:51:34 +00:10  127.0.0.1@localhost/overlay/flaky/2 started
23:51:34 +00:10  127.0.0.1@localhost was recovered 1 time(s) around flaky
23:51:34 +00:10  127.0.0.1@localhost/overlay/flaky/2 OK
23:51:34 +00:10  127.0.0.1@localhost sweep noisy: OK
23:51:34 +00:10  127.0.0.1@localhost/overlay/noisy/1 started
23:51:34 +00:10  127.0.0.1 restarted, seen in its own log
23:51:35 +00:11  127.0.0.1@localhost/overlay/noisy/1/1 FAIL the drive answers
23:51:35 +00:11  127.0.0.1@localhost/overlay/noisy/1 FAIL
23:51:35 +00:11  127.0.0.1@localhost/overlay/noisy/1 device state captured
23:51:35 +00:11  127.0.0.1@localhost sweep noisy: after failure,: OK
23:51:35 +00:11  127.0.0.1@localhost sweep noisy: OK
23:51:35 +00:11  127.0.0.1@localhost/overlay/noisy/2 started
23:51:36 +00:11  127.0.0.1 restarted, seen in its own log
23:51:36 +00:12  127.0.0.1@localhost/overlay/noisy/2/1 FAIL the drive answers
23:51:36 +00:12  127.0.0.1@localhost/overlay/noisy/2 FAIL
23:51:36 +00:12  127.0.0.1@localhost/overlay/noisy/2 device state captured
23:51:37 +00:12  127.0.0.1@localhost sweep noisy: after failure,: OK
23:51:37 +00:12  127.0.0.1@localhost sweep noisy: OK
23:51:37 +00:12  127.0.0.1@localhost/overlay/noisy/3 started
23:51:37 +00:12  127.0.0.1 restarted, seen in its own log
23:51:38 +00:13  127.0.0.1@localhost/overlay/noisy/3/1 FAIL the drive answers
23:51:38 +00:13  127.0.0.1@localhost/overlay/noisy/3 FAIL
23:51:38 +00:13  127.0.0.1@localhost/overlay/noisy/3 device state captured
23:51:38 +00:13  127.0.0.1@localhost sweep noisy: after the last attempt,: OK
23:51:38 +00:13  127.0.0.1@localhost sweep browse: OK
23:51:38 +00:13  127.0.0.1@localhost/overlay/browse/1 started
23:51:38 +00:13  127.0.0.1@localhost/overlay/browse/1 PUT /v1/machine:menu_button
23:51:38 +00:14  127.0.0.1@localhost/overlay/browse/1 FAIL
23:51:38 +00:14  127.0.0.1@localhost/overlay/browse/1 device state captured
23:51:39 +00:14  127.0.0.1@localhost sweep browse: after failure,: OK
23:51:39 +00:14  127.0.0.1@localhost sweep browse: OK
23:51:39 +00:14  127.0.0.1@localhost/overlay/browse/2 started
23:51:39 +00:14  127.0.0.1@localhost/overlay/browse/2 PUT /v1/machine:menu_button
23:51:39 +00:15  127.0.0.1@localhost/overlay/browse/2 FAIL
23:51:39 +00:15  127.0.0.1@localhost/overlay/browse/2 device state captured
23:51:39 +00:15  127.0.0.1@localhost sweep browse: after failure,: OK
23:51:39 +00:15  127.0.0.1@localhost sweep browse: OK
23:51:39 +00:15  127.0.0.1@localhost/overlay/browse/3 started
23:51:40 +00:15  127.0.0.1@localhost/overlay/browse/3 PUT /v1/machine:menu_button
23:51:40 +00:16  127.0.0.1@localhost/overlay/browse/3 FAIL
23:51:40 +00:16  127.0.0.1@localhost/overlay/browse/3 device state captured
23:51:40 +00:16  127.0.0.1@localhost sweep browse: after the last attempt,: OK
23:51:40 +00:16  127.0.0.1@localhost sweep menu-left-open: OK
23:51:40 +00:16  127.0.0.1@localhost/overlay/menu-left-open/1 started
23:51:40 +00:16  127.0.0.1@localhost/overlay/menu-left-open/1 OK
23:51:40 +00:16  127.0.0.1@localhost sweep menu-closed-again: OK
23:51:40 +00:16  127.0.0.1@localhost/overlay/menu-closed-again/1 started
23:51:40 +00:16  127.0.0.1@localhost/overlay/menu-closed-again/1 OK
23:51:40 +00:16  127.0.0.1@localhost sweep leaves-things-behind: OK
23:51:40 +00:16  127.0.0.1@localhost/overlay/leaves-things-behind/1 started
23:51:41 +00:16  127.0.0.1@localhost/overlay/leaves-things-behind/1 PUT /v1/drives/a:mount {"image": "/Usb0/game.d64"}
23:51:41 +00:16  127.0.0.1@localhost/overlay/leaves-things-behind/1 PUT /v1/configs/Network%20Settings/Log%20to%20Syslog%20Server {"value": "192.168.1.2:5514"}
23:51:41 +00:16  127.0.0.1@localhost/overlay/leaves-things-behind/1 OK
23:51:41 +00:16  127.0.0.1@localhost/overlay/missing-file/1 started
23:51:41 +00:16  127.0.0.1@localhost/overlay/missing-file/1 SKIP: missing /tmp/e2e-observability-fixture-cfpfp5j1/suites/missing_file.py
23:51:41 +00:16  127.0.0.1@localhost sweep cut-short: OK
23:51:41 +00:16  127.0.0.1@localhost/overlay/cut-short/1 started
23:51:41 +00:16  127.0.0.1@localhost/overlay/cut-short/1 incomplete
23:51:41 +00:16  the run warned: device log: 127.0.0.1@localhost sent no line at all during this run, so its log is empty; the collector received 498 line(s) in total on UDP 47716, and this run expected its lines from no address. The setting this run read at both ends, and whether anything reached syslog-unknown-sender.txt, are the facts that tell one silence from another; nothing here says whether the device sent lines that never arrived

## Checks

### 127.0.0.1, the runner itself

Checks the runner reported outside any suite, which is its teardown after the last suite of a mode.

| # | Check                                                             | Verdict | Duration | Opened at           | Closed at           | Reported               |
| - | ----------------------------------------------------------------- | ------- | -------- | ------------------- | ------------------- | ---------------------- |
| 0 | settings: capture 127.0.0.1                                       | OK      | 0.003s   | 2026-09-02 22:51:27 | 2026-09-02 22:51:27 | 2 settings in 2 stores |
| 0 | recovery: rm -f /tmp/e2e-observability-fixture-cfpfp5j1/unhealthy | OK      | 0.004s   | 2026-09-02 22:51:28 | 2026-09-02 22:51:28 | -                      |
| 0 | recovery: device answers again                                    | OK      | 0.001s   | 2026-09-02 22:51:28 | 2026-09-02 22:51:28 | -                      |
| 0 | TEARDOWN (overlay): release input                                 | OK      | 0.001s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -                      |
| 0 | TEARDOWN (overlay): close active menu UI                          | OK      | 0.002s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -                      |
| 0 | TEARDOWN (overlay): reset machine                                 | OK      | 0.003s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -                      |
| 0 | settle: device answers before perf                                | OK      | 0.001s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -                      |
| 0 | settings: restore 127.0.0.1                                       | OK      | 0.003s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | 1 put back             |

### 127.0.0.1@localhost, the runner itself

Checks the runner reported outside any suite, which is its teardown after the last suite of a mode.

| # | Check                                                             | Verdict | Duration | Opened at           | Closed at           | Reported                                                                                                                       |
| - | ----------------------------------------------------------------- | ------- | -------- | ------------------- | ------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| 0 | settings: capture 127.0.0.1                                       | OK      | 0.003s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | 2 settings in 2 stores                                                                                                         |
| 0 | settings: capture localhost                                       | OK      | 0.004s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | 2 settings in 2 stores                                                                                                         |
| 0 | cartridge: the computer prefers its external cartridge            | WARN    | 0.001s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | localhost did not answer for 'Cartridge Preference': GET its config API returned HTTP 404: b'{"errors": ["no such category"]}' |
| 0 | cartridge: the computer's own drives are off                      | OK      | 0.002s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | already off                                                                                                                    |
| 0 | recovery: rm -f /tmp/e2e-observability-fixture-cfpfp5j1/unhealthy | OK      | 0.004s   | 2026-09-02 22:51:34 | 2026-09-02 22:51:34 | -                                                                                                                              |
| 0 | recovery: device answers again                                    | OK      | 0.001s   | 2026-09-02 22:51:34 | 2026-09-02 22:51:34 | -                                                                                                                              |

### 127.0.0.1/overlay/held/1

**the ordinary case**

| # | Check                       | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the listing is complete     | OK      | 0.000s   | 2026-09-02 22:51:27 | 2026-09-02 22:51:27 | 20 rows  |
| 2 | the first row is the header | OK      | 0.000s   | 2026-09-02 22:51:27 | 2026-09-02 22:51:27 | -        |

### 127.0.0.1/overlay/broken/1

| # | Check                      | Verdict | Duration | Opened at           | Closed at           | Reported                                                                |
| - | -------------------------- | ------- | -------- | ------------------- | ------------------- | ----------------------------------------------------------------------- |
| 1 | the row survives a redraw  | FAIL    | 0.000s   | 2026-09-02 22:51:27 | 2026-09-02 22:51:27 | 0 rows, expected 20                                                     |
| 2 | the name is listed in full | SKIP    | 0.000s   | 2026-09-02 22:51:27 | 2026-09-02 22:51:27 | needs the ftp-listing-full-length fix, which this machine does not have |

### 127.0.0.1/overlay/broken/2

| # | Check                      | Verdict | Duration | Opened at           | Closed at           | Reported                                                                |
| - | -------------------------- | ------- | -------- | ------------------- | ------------------- | ----------------------------------------------------------------------- |
| 1 | the row survives a redraw  | FAIL    | 0.000s   | 2026-09-02 22:51:27 | 2026-09-02 22:51:27 | 0 rows, expected 20                                                     |
| 2 | the name is listed in full | SKIP    | 0.000s   | 2026-09-02 22:51:27 | 2026-09-02 22:51:27 | needs the ftp-listing-full-length fix, which this machine does not have |

### 127.0.0.1/overlay/broken/3

| # | Check                      | Verdict | Duration | Opened at           | Closed at           | Reported                                                                |
| - | -------------------------- | ------- | -------- | ------------------- | ------------------- | ----------------------------------------------------------------------- |
| 1 | the row survives a redraw  | FAIL    | 0.000s   | 2026-09-02 22:51:28 | 2026-09-02 22:51:28 | 0 rows, expected 20                                                     |
| 2 | the name is listed in full | SKIP    | 0.000s   | 2026-09-02 22:51:28 | 2026-09-02 22:51:28 | needs the ftp-listing-full-length fix, which this machine does not have |

### 127.0.0.1/overlay/flaky/1

| # | Check              | Verdict | Duration | Opened at           | Closed at           | Reported             |
| - | ------------------ | ------- | -------- | ------------------- | ------------------- | -------------------- |
| 1 | the device is well | FAIL    | 0.000s   | 2026-09-02 22:51:28 | 2026-09-02 22:51:28 | the listener is gone |

### 127.0.0.1/overlay/flaky/2

| # | Check              | Verdict | Duration | Opened at           | Closed at           | Reported  |
| - | ------------------ | ------- | -------- | ------------------- | ------------------- | --------- |
| 1 | the device is well | OK      | 0.000s   | 2026-09-02 22:51:28 | 2026-09-02 22:51:28 | recovered |

### 127.0.0.1/overlay/noisy/1

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | the drive answers | FAIL    | 0.908s   | 2026-09-02 22:51:29 | 2026-09-02 22:51:30 | the drive did not answer |

### 127.0.0.1/overlay/noisy/2

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | the drive answers | FAIL    | 0.908s   | 2026-09-02 22:51:30 | 2026-09-02 22:51:31 | the drive did not answer |

### 127.0.0.1/overlay/noisy/3

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | the drive answers | FAIL    | 0.922s   | 2026-09-02 22:51:31 | 2026-09-02 22:51:32 | the drive did not answer |

### 127.0.0.1/overlay/browse/1

| # | Check            | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | ---------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the cursor moves | OK      | 0.018s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | one row  |

### 127.0.0.1/overlay/menu-left-open/1

| # | Check          | Verdict | Duration | Opened at           | Closed at           | Reported  |
| - | -------------- | ------- | -------- | ------------------- | ------------------- | --------- |
| 1 | the menu opens | OK      | 0.000s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | on screen |

### 127.0.0.1/overlay/menu-closed-again/1

| # | Check           | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the menu closes | OK      | 0.000s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -        |

### 127.0.0.1/overlay/leaves-things-behind/1

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the image mounts  | OK      | 0.027s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -        |
| 2 | the setting takes | OK      | 0.001s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -        |

### 127.0.0.1/overlay/cut-short/1

| # | Check           | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the first half  | OK      | 0.000s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -        |
| 2 | the second half | OK      | 0.000s   | 2026-09-02 22:51:32 | 2026-09-02 22:51:32 | -        |

### 127.0.0.1/perf/a-benchmark/1

| # | Check                    | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ------------------------ | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | typing reaches the field | OK      | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | 11.2 characters a second |

### 127.0.0.1@localhost/overlay/held/1

**the ordinary case**

| # | Check                       | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the listing is complete     | OK      | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | 20 rows  |
| 2 | the first row is the header | OK      | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | -        |

### 127.0.0.1@localhost/overlay/broken/1

| # | Check                      | Verdict | Duration | Opened at           | Closed at           | Reported                                                                |
| - | -------------------------- | ------- | -------- | ------------------- | ------------------- | ----------------------------------------------------------------------- |
| 1 | the row survives a redraw  | FAIL    | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | 0 rows, expected 20                                                     |
| 2 | the name is listed in full | SKIP    | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | needs the ftp-listing-full-length fix, which this machine does not have |

### 127.0.0.1@localhost/overlay/broken/2

| # | Check                      | Verdict | Duration | Opened at           | Closed at           | Reported                                                                |
| - | -------------------------- | ------- | -------- | ------------------- | ------------------- | ----------------------------------------------------------------------- |
| 1 | the row survives a redraw  | FAIL    | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | 0 rows, expected 20                                                     |
| 2 | the name is listed in full | SKIP    | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | needs the ftp-listing-full-length fix, which this machine does not have |

### 127.0.0.1@localhost/overlay/broken/3

| # | Check                      | Verdict | Duration | Opened at           | Closed at           | Reported                                                                |
| - | -------------------------- | ------- | -------- | ------------------- | ------------------- | ----------------------------------------------------------------------- |
| 1 | the row survives a redraw  | FAIL    | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | 0 rows, expected 20                                                     |
| 2 | the name is listed in full | SKIP    | 0.000s   | 2026-09-02 22:51:33 | 2026-09-02 22:51:33 | needs the ftp-listing-full-length fix, which this machine does not have |

### 127.0.0.1@localhost/overlay/flaky/1

| # | Check              | Verdict | Duration | Opened at           | Closed at           | Reported             |
| - | ------------------ | ------- | -------- | ------------------- | ------------------- | -------------------- |
| 1 | the device is well | FAIL    | 0.000s   | 2026-09-02 22:51:34 | 2026-09-02 22:51:34 | the listener is gone |

### 127.0.0.1@localhost/overlay/flaky/2

| # | Check              | Verdict | Duration | Opened at           | Closed at           | Reported  |
| - | ------------------ | ------- | -------- | ------------------- | ------------------- | --------- |
| 1 | the device is well | OK      | 0.000s   | 2026-09-02 22:51:34 | 2026-09-02 22:51:34 | recovered |

### 127.0.0.1@localhost/overlay/noisy/1

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | the drive answers | FAIL    | 0.916s   | 2026-09-02 22:51:34 | 2026-09-02 22:51:35 | the drive did not answer |

### 127.0.0.1@localhost/overlay/noisy/2

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | the drive answers | FAIL    | 0.933s   | 2026-09-02 22:51:36 | 2026-09-02 22:51:36 | the drive did not answer |

### 127.0.0.1@localhost/overlay/noisy/3

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported                 |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | ------------------------ |
| 1 | the drive answers | FAIL    | 0.914s   | 2026-09-02 22:51:37 | 2026-09-02 22:51:38 | the drive did not answer |

### 127.0.0.1@localhost/overlay/menu-left-open/1

| # | Check          | Verdict | Duration | Opened at           | Closed at           | Reported  |
| - | -------------- | ------- | -------- | ------------------- | ------------------- | --------- |
| 1 | the menu opens | OK      | 0.000s   | 2026-09-02 22:51:40 | 2026-09-02 22:51:40 | on screen |

### 127.0.0.1@localhost/overlay/menu-closed-again/1

| # | Check           | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | --------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the menu closes | OK      | 0.000s   | 2026-09-02 22:51:40 | 2026-09-02 22:51:40 | -        |

### 127.0.0.1@localhost/overlay/leaves-things-behind/1

| # | Check             | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | ----------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the image mounts  | OK      | 0.041s   | 2026-09-02 22:51:41 | 2026-09-02 22:51:41 | -        |
| 2 | the setting takes | OK      | 0.001s   | 2026-09-02 22:51:41 | 2026-09-02 22:51:41 | -        |

### 127.0.0.1@localhost/overlay/cut-short/1

| # | Check          | Verdict | Duration | Opened at           | Closed at           | Reported |
| - | -------------- | ------- | -------- | ------------------- | ------------------- | -------- |
| 1 | the first half | OK      | 0.000s   | 2026-09-02 22:51:41 | 2026-09-02 22:51:41 | -        |

## Where the time went

Slowest suite runs:

| Suite run                                          | Duration |
| -------------------------------------------------- | -------- |
| 127.0.0.1@localhost/overlay/noisy/2                | 1.043s   |
| 127.0.0.1@localhost/overlay/noisy/3                | 1.030s   |
| 127.0.0.1@localhost/overlay/noisy/1                | 1.016s   |
| 127.0.0.1/overlay/noisy/3                          | 1.015s   |
| 127.0.0.1/overlay/noisy/1                          | 0.995s   |
| 127.0.0.1/overlay/noisy/2                          | 0.994s   |
| 127.0.0.1@localhost/overlay/browse/2               | 0.734s   |
| 127.0.0.1@localhost/overlay/browse/1               | 0.706s   |
| 127.0.0.1@localhost/overlay/browse/3               | 0.706s   |
| 127.0.0.1@localhost/overlay/leaves-things-behind/1 | 0.164s   |

Slowest checks:

| Check                                                | Label             | Duration |
| ---------------------------------------------------- | ----------------- | -------- |
| 127.0.0.1@localhost/overlay/noisy/2/1                | the drive answers | 0.933s   |
| 127.0.0.1/overlay/noisy/3/1                          | the drive answers | 0.922s   |
| 127.0.0.1@localhost/overlay/noisy/1/1                | the drive answers | 0.916s   |
| 127.0.0.1@localhost/overlay/noisy/3/1                | the drive answers | 0.914s   |
| 127.0.0.1/overlay/noisy/2/1                          | the drive answers | 0.908s   |
| 127.0.0.1/overlay/noisy/1/1                          | the drive answers | 0.908s   |
| 127.0.0.1@localhost/overlay/leaves-things-behind/1/1 | the image mounts  | 0.041s   |
| 127.0.0.1/overlay/leaves-things-behind/1/1           | the image mounts  | 0.027s   |
| 127.0.0.1/overlay/browse/1/1                         | the cursor moves  | 0.018s   |
| 127.0.0.1@localhost/overlay/leaves-things-behind/1/2 | the setting takes | 0.001s   |

## Device log

The device log is best-effort and incomplete by construction. It is UDP with no retransmission, the firmware's 16 KB forwarding buffer discards itself whole on overflow, output is throttled to about 200 lines a second, and an assertion failure arrives only from firmware that flushes it from the failing task. A line's time is when this host received it, which lags when the firmware printed it by an unbounded amount, so these are lines received during a check and not lines the device produced during it.

Each target's log was expected from the addresses its machines resolve to, and arrived from these. A target whose lines were attributed by the port they arrived on is attributed correctly whatever address they came from, and the addresses are still shown so a device logging from an unexpected one stays visible.

| Target              | Expected from | Collected on | Arrived from      | Attributed by |
| ------------------- | ------------- | ------------ | ----------------- | ------------- |
| 127.0.0.1           | `127.0.0.1`   | `47716`      | `127.0.0.1` (498) | address (498) |
| 127.0.0.1@localhost | -             | `47716`      | none              | -             |

What the collector reported about this run:

- localhost and 127.0.0.1 are both 127.0.0.1, so a datagram from it is attributed to 127.0.0.1 unless the port it arrived on says otherwise
- localhost resolves only to addresses another machine already claims and shares its syslog port, so its lines cannot be attributed and land in syslog-unknown-sender.txt
- 127.0.0.1 sent nothing when this run asked it for /v1/version, so its log is not reaching the collector on UDP 47716
- localhost sent nothing when this run asked it for /v1/version, so its log is not reaching the collector on UDP 47716
- 127.0.0.1@localhost sent no line at all during this run, so its log is empty; the collector received 498 line(s) in total on UDP 47716, and this run expected its lines from no address. The setting this run read at both ends, and whether anything reached syslog-unknown-sender.txt, are the facts that tell one silence from another; nothing here says whether the device sent lines that never arrived

### 127.0.0.1

498 line(s) received, from `127.0.0.1/syslog.txt`.

**127.0.0.1/overlay/noisy/1/1**, from the end of the check before it. 83 line(s) in the window, 80 of them this run's own requests, which are in the file and not here:

```
All linked modules have been initialized and are now running.
1541: seek track 18
1541: no answer from the drive
```

**127.0.0.1/overlay/noisy/2/1**, from the end of the check before it. 83 line(s) in the window, 80 of them this run's own requests, which are in the file and not here:

```
All linked modules have been initialized and are now running.
1541: seek track 18
1541: no answer from the drive
```

**127.0.0.1/overlay/noisy/3/1**, from the end of the check before it. 83 line(s) in the window, 80 of them this run's own requests, which are in the file and not here:

```
All linked modules have been initialized and are now running.
1541: seek track 18
1541: no answer from the drive
```
