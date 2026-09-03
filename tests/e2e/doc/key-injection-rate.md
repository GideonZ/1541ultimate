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
batch size. Both machines, both destinations, as first measured:

| Target | Destination | 16 keys | 32 keys | 64 keys |
| --- | --- | ---: | ---: | ---: |
| u64 | mcm | 51.0 ms/key | 51.6 ms/key | lost 1 |
| u64 | basic | - | 99.5 ms/key | 100.4 ms/key |
| u2@c64u | mcm | 101.0 ms/key | 100.4 ms/key | 100.5 ms/key |
| u2@c64u | basic | 105.9 ms/key | 103.2 ms/key | 100.6 ms/key |

The same measurement after the two firmware changes below, in batches of 64:

| Target | Destination | Rate | Arrived |
| --- | --- | ---: | --- |
| u64 | mcm | 51.1, 51.7, 52.2 ms/key | 64 of 64, three runs |
| u64 | basic | 60.6, 60.6, 60.8 ms/key | 64 of 64, three runs |
| u2@c64u | mcm | 100.7, 100.8 ms/key | 64 of 64, two runs |
| u2@c64u | basic | 99.9, 101.0 ms/key | 64 of 64, two runs |

The `u2@c64u` rates are unchanged on purpose. Those keys are injected into the
keyboard matrix of the C64 Ultimate the cartridge is plugged into, and that
computer runs its own released firmware (1.2.0 on this bench), which this
branch does not build or flash. The tick change below is in firmware we flash,
so it moves the machines we flash and nothing else.

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
gave 60.7 ms/key and 1+1 gave 41.3 ms/key, each lossless over 64-key batches.

### The queue ticks now shipped

`REST_KEYBOARD_TAP_HOLD_QUEUE_TICKS` is 2 and `REST_TAP_GAP_TICKS` is 1, which
measured 60.6 to 60.8 ms/key over three 64-key batches and lost nothing in a
560-key BASIC soak on an Ultimate 64.

The reason to stop at 2+1 rather than 1+1 is what each tick has to cover. A
tick is 20 ms (`REST_INPUT_TIMER_TICKS`, `keyboard_usb.cc:42`) and the C64
KERNAL scans the keyboard once per video frame, which is also about 20 ms. A
two-tick hold therefore puts the key in front of at least two scans, and the
one-tick gap puts the release in front of at least one, so neither depends on
where a tick happens to fall relative to a frame. At 1+1 the key is in front of
a single scan and a hold that drifted against the frame could miss it. 1+1 was
measured as lossless here, but on one machine and over one soak, and the margin
it removes is not one this bench can observe directly.

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
lost at 64 - is what the table above measures.

### The fix

The ring is now one slot larger than the batch limit, rather than the same
size as it:

```c
static const int USB_KEY_BUFFER_SIZE = 64;
static const int USB_INJECTED_BUFFER_SIZE = USB_KEY_BUFFER_SIZE + 1;
```

`injected_buffer` and the four wrap tests that walk it use the new constant;
`key_buffer`, which holds what a USB keyboard typed and is not what the input
API writes into, keeps the old one. The published limit does not move: the API
still accepts 64 events and the ring now holds 64 keys.

`usb_keyboard_queue_test.cpp` covers it twice. `AFullInputApiBatchIsNotDropped`
pushes 64 keys and reads all 64 back, and fails with the ring at its old size.
`PushHeadRepeatIsBounded` asks for more than the ring holds and counts what
comes back, which is what pins the capacity.

On hardware, a 64-key batch into the monitor destination arrived complete on
every run, and a 640-key soak in batches of 64 lost nothing. Before the fix the
same batch lost exactly one key every time.

## What the harness adopted

`tests/lib/api.py` sets `MAX_INPUT_EVENTS = 64`. The gate suite now sends one
batch of 64 rather than ten keys, so the size that used to lose a key is the
size the gate exercises.

### The second limit, which the event count does not imply

`machine:input` has a body-size limit as well as an event limit:
`INPUT_JSON_BODY_MAX_SIZE` is 4096 bytes (`route_input.cc:32`), and a longer
body is refused with HTTP 400 `JSON body is too large.` An event is not a fixed
size, so the two do not convert into one another: a tap of `"a"` serialises to
about 55 bytes and a tap of `"inst_del"` to 62, which puts 64 backspaces at
4110 bytes and 64 letters at 3852.

Raising the harness batch limit from a hard-coded 60 to the API's 64 therefore
broke the file browser, whose field clear is exactly 64 backspaces
(`EDIT_FIELD_CLEAR_TAPS`). Every context-menu action that types into a field -
Rename, Move to..., Delete - failed, and because the rename dialog was left
open, the navigation after them failed too. The whole suite reported `could not
return to '/'`, which names neither the request nor the limit.

Batching now measures rather than counts. `api.input_batches` fills a batch up
to whichever limit binds first, using the same serialisation the transport
sends, and `tests/e2e/lib/ui_backend.py` and `tests/e2e/lib/menu.py` both use
it. `api.MachineApi.send_input` also refuses an oversized body itself, naming
the limit and the splitter, rather than letting the device answer HTTP 400.

That also closes a case the old constant did not cover. A cursor-key tap is two
inputs and 83 bytes, so 60 of them come to 5023 bytes: a batch of 60 cursor
keys would have been refused for the same reason. No suite sends that many at
once today, which is why it had not been seen.

`tests/lib/input_batching_test.py` covers both limits and needs no device.
Making the splitter count events only makes it fail on the long-key case.

`KEY_DRAIN_SECONDS` is 20 ms for a device target, below the 51 ms measured for
the u64 menu, which is safe because every suite polls for the state it expects
rather than trusting the wait alone. `SPLIT_KEY_DRAIN_SECONDS` is 60 ms; the
next section is why it is not the 100 ms measured above.

## What the charged drain does and does not control

`SPLIT_KEY_DRAIN_SECONDS` was set to 100 ms to match the delivery rate measured
for a cartridge target. That conflated two different things. The rate above is
how fast the firmware delivers a batch. The constant is how long the harness
waits before reading the result, and the two are not the same number: the whole
batch is posted in one request and drains at the firmware's own rate whatever
is charged here, and `RestBackend._settle` spends time watching the screen
first, so the constant only tops that up.

Sweeping it settles what it is worth. Six settings from 30 ms to 100 ms a key
were run on `u2@c64u` through
`tests/soak/filemanager/menu_navigation_soak_test.py`, which jumps a known
distance in a listing of files named for their own index and reads back where
the cursor landed. That fails on a lost key rather than on a slow one, which is
the failure the constant is supposed to prevent.

| Charged | Wrong landings, out of 30 movements |
| ---: | --- |
| 30 ms | 0 |
| 40 ms | 0 |
| 50 ms | 1 |
| 60 ms | 2 |
| 80 ms | 0 |
| 100 ms | 2 |

The two cleanest passes are the two fastest settings and the slowest setting
lost two, so the loss does not follow the charge. What it follows is how many
keys the movement took: every wrong landing but one came from a burst of 12 to
38 single cursor keys, and across the sweep a key went missing a few times in a
thousand at every setting.

The constant therefore stays at 60 ms, which is what this tree's own firmware
ticks measure, and no finer tuning is attempted because the sweep shows there
is nothing there to tune. The lever that does move both speed and reliability
is the number of keys a movement takes. `Browser.move_rows` spends page keys on
the bulk of a jump and single steps on the remainder, in one request: on
`u2@c64u` a 38-row landing costs 8 keys instead of 38, and its median time fell
from 2173 ms to 820 ms. `Browser.fill_edit_field` empties a string field with
one KEY_CLEAR instead of up to 64 backspaces, for the same reason.

## What a keystroke costs the harness

The rates above are what the device does with a key. They are not what a suite
pays for one, and the difference is most of a run. Measured on the Ultimate 64
over the REST backend, `RestBackend.send_key` cost 411 to 465 ms a key and
issued 9.3 requests for it, of which 45% of the wall clock was on the wire and
the rest was the harness sleeping. The suite that pays this most is the
machine-code monitor: one recorded run made 3670 requests over 575 s, of which
59 s was request time.

What the device actually needs is much less. Polling the menu screen as fast as
the transport allows after an injected key, over 15 keys on an Ultimate 64: the
first changed byte appeared 28 to 41 ms after the request returned, and the
last change of the redraw landed by 47 to 65 ms. An idle menu screen does not
change on its own, which was checked before relying on it: 60 reads over 3 s
returned one distinct screen, so a screen that differs from the last one read
differs because a key changed it.

Three costs were measured and two were removed:

- `wait_screen_settled` re-read a screen `wait_screen_changes` had just read,
  to use as its first sample. It now takes that screen as `known`. The rule it
  applies is unchanged: it still requires `SETTLE_STABLE_SAMPLES` further reads
  that match.
- `_settle` ended by calling `capture()`, which read the screen a further time
  to be told what the settle had just proved. `wait_screen_settled` now returns
  the screen it stopped on and `_settle` decodes that.
- `wait_screen_changes` slept a full `POLL_INTERVAL_SECONDS` between reads even
  when that pushed the last of its `KEY_CHANGE_MIN_SAMPLES` reads past the
  budget. With 6 samples, a 0.3 s budget and an 18 ms read, a keypress that
  changed nothing cost 408 ms: 108 ms of reads and 250 ms of five full pauses.
  The pause is now shortened to fit the remaining reads into the remaining
  budget. Both of the conditions that end the wait are untouched, so it still
  ends only once the budget has elapsed and the reads have been taken, and on a
  device slow enough that its reads alone exceed the budget the pause goes to
  zero and the wait is exactly what it was.

Requests per key fell from 9.3 to 7.8 where the keys mostly changed nothing and
to 4.5 where every key changed the screen, at 222 to 226 ms a key for the
latter.

Keeping the HTTP connection open was measured and rejected. Every REST request
in a run opens a new connection (5928 of 5928 in one recorded run), but the
device answers `Connection: close`, so a client that offers to keep the
connection reconnects anyway: 40 reads took a mean of 18.5 ms on new
connections and 18.3 ms through a client asking to reuse one. The 18 ms is the
device's own work, not the handshake.

`POLL_INTERVAL_SECONDS` was left at 0.05. The settle is the largest cost left,
at about 155 ms a key for its two-sample quiet window, and shortening that
window is what the earlier calibration sweep found breaks first.

## What this does and does not explain

It explains a lost key in any batch of exactly 64, which no suite in the tree
sent before the gate suite was changed to send one.

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
- The 2+1 tick rate was soaked on one destination of one machine: 560 keys into
  BASIC on the Ultimate 64. The argument that two ticks cover two KERNAL scans
  is a reading of the tick period against the frame rate, not a measurement of
  the scan itself.
- The harness costs above are from an Ultimate 64 driving the file browser over
  the REST backend. A screen that redraws more of itself per key, or a device
  under load, moves them.
