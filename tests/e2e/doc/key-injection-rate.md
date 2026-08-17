# How fast keys can be injected, and where they start going missing

## Why this was measured

The E2E suites drive the device by injecting keystrokes over `POST
/v1/machine:input` and reading the result back from a screen or from memory.
Two things about that were assumed rather than measured:

1. How long to wait for an injected key, expressed as `key_drain_seconds` in
   `tests/lib/pacing.py`.
2. How many keys one request may carry, expressed as `MAX_INPUT_EVENTS` in
   `tests/lib/api.py`.

Both mattered because the machine-code-monitor suite was failing
intermittently on `u2@c64u` with symptoms that all reduce to one lost
keystroke: `the field reads '180-1183,1200' after '1180-1183,1200' was typed`,
`J E000 landed on $118B`, `W did not set the Hex view to 8 bytes per row`. A
lost keystroke fails whichever check happened to be typing at the time, so the
failures looked unrelated to each other and none of them named a rate.

## Instrument

`tests/e2e/io/c64/key_injection_test.py`. It types a known sequence and reads
back the memory the machine should hold, so what is compared is the machine's
own record rather than a screen predicate.

Two destinations, because the firmware delivers to them by different paths
(`software/api/route_input.cc`, `apply_keyboard_event`):

| Destination | Path | Oracle |
| --- | --- | --- |
| `basic` | Menu closed: keys are queued into the C64 keyboard matrix and the KERNAL scans them on its own interrupt | C64 screen memory at `$0400`, which BASIC echoes each received character into |
| `mcm` | Menu open: keys go to the menu's own keyboard state and never reach the matrix | The page the monitor's ASCII view edits, at `$C000`, which edit mode writes one character per address into |

Only tapped keys are measured. An explicit press/release pair was tried and
dropped: it lost 3 of 10 keys reproducibly at every hold duration tried, which
contradicts an earlier reading of 30 of 30 from the same pair, and no
explanation for the difference was established. A measurement that cannot be
defended is not reported as one.

Controls used throughout:

- A warm-up of 12 keys after the destination is opened, discarded and reported
  separately. The keys immediately following a reset are not representative:
  in one run 9 of the first 10 went missing and then nothing was lost for the
  remaining 130.
- Arrival is polled to a 25 s deadline rather than read once. A key that has
  been sent is not yet a key that has been seen, and a single read straight
  after the send reports most of a fast run as lost when nothing was lost.
  This is what separates "late" from "gone".
- The comparison is against the exact byte the machine should hold. An earlier
  version of this instrument compared the monitor's page against upper-case
  ASCII while edit mode had written the lower-case that the injected key
  actually produces, and reported 396 of 420 keys lost when none had been.
  That is the single most dangerous failure mode here: a broken oracle looks
  exactly like a broken device.

## Delivery rate

Time from posting one batch until every key of it is readable, divided by the
batch size. Both machines, both destinations:

| Target | Destination | 16 keys | 32 keys | 64 keys |
| --- | --- | ---: | ---: | ---: |
| u64 | mcm | 51.0 ms/key | 51.6 ms/key | lost 1 |
| u64 | basic | - | 99.5 ms/key | 100.4 ms/key |
| u2@c64u | mcm | 101.0 ms/key | 100.4 ms/key | 100.5 ms/key |
| u2@c64u | basic | 105.9 ms/key | 103.2 ms/key | 100.6 ms/key |

The POST itself is not the cost: a 32-key batch posts in 53 ms and takes
3.2 s to arrive, so the wire is about 1 ms a key against the device's own
pacing. Sending one request per key, as the first version of this instrument
did, therefore buys nothing and costs a round trip each time.

Two rates appear, and they correspond to the two delivery paths:

- **100 ms/key** wherever the keys must cross the C64's keyboard matrix. This
  is `REST_KEYBOARD_TAP_HOLD_QUEUE_TICKS = 3` plus `REST_TAP_GAP_TICKS = 2`
  (`route_input.cc:169`, `keyboard_usb.cc:50`), five ticks of a queue drained
  once per video frame. It applies to `basic` on both machines, and to `mcm`
  on `u2@c64u`, where the menu belongs to the cartridge and its keys are
  injected into the host computer's matrix for the cartridge to scan.
- **51 ms/key** for `mcm` on the u64, where the menu is the device's own and
  injected keys are pushed straight into its key buffer without touching the
  matrix.

A firmware experiment confirmed the first figure is that queue and not the REST
tap hold: halving `REST_KEYBOARD_TAP_HOLD_TICKS` (60 ms to 20 ms) and
`REST_KEYBOARD_TAP_GAP_TICKS` (40 ms to 20 ms) left the rate at 100 ms/key
unchanged, because the menu-closed path does not use those constants at all.
Reducing the queue ticks instead did move it: 3+2 ticks gave 100 ms/key, 2+1
gave 60.7 ms/key and 1+1 gave 41.3 ms/key, each lossless over 64-key batches,
and a 560-key BASIC soak at 1+1 lost nothing. That change is **not** shipped
here: it shortens the window in which the C64 can see a key to a single video
frame, it benefits only machines whose firmware we flash (a cartridge's keys
are injected by its host computer, whose firmware is not ours to change), and
the soak behind it covers one destination on one machine. It is recorded so
the next person knows where the 100 ms comes from and what moving it costs.

## Where keys start going missing

Batch size, three runs each, u64, `mcm` destination:

| Batch | Keys lost, run 1 / 2 / 3 |
| ---: | --- |
| 32 | 0 / 0 / 0 |
| 40 | 0 / 0 / 0 |
| 48 | 0 / 0 / 0 |
| 56 | 0 / 0 / 0 |
| 60 | 0 / 0 / 0 |
| 62 | 0 / 0 / 0 |
| 63 | 0 / 0 / 0 |
| 64 | 1 / 1 / 1 |

The boundary is exact and reproducible: 63 keys always arrive, 64 keys always
lose exactly one.

## Mechanism

`INPUT_API_MAX_EVENTS = 64` (`software/api/input_api.h:10`), and
`input_api_validate_batch` accepts a batch of exactly 64. With the menu up,
`apply_keyboard_menu_event` (`route_input.cc:330`) pushes each key through
`Keyboard_USB::push_head_repeat` (`keyboard_usb.cc:556`):

```c
int next_head = injected_head + 1;
if (next_head == USB_KEY_BUFFER_SIZE) {
    next_head = 0;
}
if (next_head == injected_tail) {
    break;                      // silently drops the key
}
```

That is a head/tail ring which distinguishes full from empty by leaving one
slot unused, so with `USB_KEY_BUFFER_SIZE = 64` (`keyboard_usb.h:18`) it holds
63 keys, not 64. The API therefore accepts one more key per request than the
buffer behind it can hold, and the surplus key is dropped without an error:
the request still answers HTTP 200.

The prediction that follows from reading the code - lossless at 63, exactly one
lost at 64 - is what the table above measures. This is a firmware defect rather
than a harness one, and it is reported here rather than fixed: the fix is
either to raise `USB_KEY_BUFFER_SIZE` or to lower `INPUT_API_MAX_EVENTS`, both
of which change a published API limit and belong with the input API rather than
with a monitor change.

## What was adopted

`tests/lib/api.py` now sets `MAX_INPUT_EVENTS = 63`, so the harness never posts
a batch the device cannot hold. No caller in the tree sends batches near that
size, so nothing else changed.

The pacing constants in `tests/lib/pacing.py` were left alone, and the
measurements say why: `SPLIT_KEY_DRAIN_SECONDS` is 100 ms, which is exactly the
delivery rate measured for a cartridge target on both destinations, so it is
neither optimistic nor wasteful. `KEY_DRAIN_SECONDS` is 20 ms for a device
target, below the 51 ms measured for the u64 menu, which is safe because every
suite polls for the state it expects rather than trusting the wait alone.

## What this does and does not explain

It explains a lost key in any batch of exactly 64, which no suite in the tree
currently sends.

It does **not** explain the `u2@c64u` monitor failures that prompted the
measurement. On that target, injection was lossless in every configuration
tried: 140 keys at 100 ms/key, 24 isolated keys at 1000 ms/key, 15 separate
runs of 10 keys at settle delays of 0 to 2 s after a reset, and batches of 16,
32 and 64 at both destinations. One burst of 9 lost keys was seen once,
immediately after the first reset following a mains power cycle of the host,
and never reproduced in 300+ keys of deliberate attempts to provoke it.

The honest conclusion is that those failures are not a function of injection
rate. They are consistent with the host wedging under sustained load, which
this bench has shown repeatedly and which clears only on mains power: three
consecutive attempts at one suite failed three different ways - `HTTP 423` on
the subsystem lock, `No route to host` with the machine off the network, and a
navigation timeout at 815 s where the same suite had taken 317 s - with health
checks passing throughout.

## Threats to validity

- One bench, one sample of each machine. The rates are properties of this
  firmware and these two devices.
- The 51 ms/key figure for the u64 menu is a mean over a batch, not a
  per-key interval; the instrument cannot see the spacing of individual keys.
- The loss boundary was established on the `mcm` destination only. The
  `basic` destination shares `push_head_repeat` only when the menu is up, so
  the same boundary is expected there but was not measured.
- Arrival is polled through `machine:readmem`, which itself takes a DMA cycle.
  That inflates the measured per-key time by an amount not separated out here;
  it cannot hide a lost key, which is what the boundary claim rests on.
