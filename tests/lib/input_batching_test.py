#!/usr/bin/env python3
# Gate check: a machine:input request never exceeds either device limit.

"""Verify api.input_batches respects the event count and the body size, no device.

`POST /v1/machine:input` has two limits and neither implies the other:

  INPUT_API_MAX_EVENTS        64 events per request (software/api/input_api.h)
  INPUT_JSON_BODY_MAX_SIZE    4096 bytes of body    (software/api/route_input.cc)

A batch that breaks the first is answered "an input batch holds 1..64 events";
one that breaks the second is answered HTTP 400 "JSON body is too large." Both
are refusals rather than silent losses, so a harness that gets the split wrong
fails whichever check happened to be typing, with a message about the request
rather than about the keys.

That is not hypothetical. Splitting on the event count alone sent the file
browser's 64-keypress field clear as one request, because 64 is within the
event limit, and the 62 bytes each `inst_del` tap serialises to put the body at
4110. Every context-menu action that renames, moves or deletes a file then
failed, and the browser was left with the rename dialog still open, so the
navigation after it failed too.

This needs no device: it measures the same serialisation the transport sends.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from api import (MAX_INPUT_BODY_BYTES, MAX_INPUT_EVENTS,  # noqa: E402
                 input_batches, input_body_bytes)
from report import Failure, check, detail, suite_fail, suite_ok  # noqa: E402

# The longest and shortest key names the REST backend actually taps, which is
# what makes the two limits bind at different batch sizes.
LONG_KEY = "cursor_up_down"
SHORT_KEY = "a"


def taps(name, count):
    return [{"kind": "keyboard", "inputs": [name], "transition": "tap"}
            for _ in range(count)]


def combo_taps(count):
    return [{"kind": "keyboard", "inputs": ["left_shift", LONG_KEY],
             "transition": "tap"} for _ in range(count)]


def assert_within_limits(batches, what):
    for index, batch in enumerate(batches):
        if len(batch) > MAX_INPUT_EVENTS:
            raise Failure(f"{what}: batch {index} carries {len(batch)} events, "
                          f"over the {MAX_INPUT_EVENTS} the API accepts")
        body = input_body_bytes(batch)
        if body > MAX_INPUT_BODY_BYTES and len(batch) > 1:
            raise Failure(f"{what}: batch {index} is {body} bytes, over the "
                          f"{MAX_INPUT_BODY_BYTES} the device accepts")


def assert_order_preserved(batches, events, what):
    flattened = [event for batch in batches for event in batch]
    if flattened != list(events):
        raise Failure(f"{what}: the batches do not carry the same events in "
                      f"the same order")


def main() -> int:
    with check("a batch of short taps is limited by the event count"):
        events = taps(SHORT_KEY, 200)
        batches = input_batches(events)
        assert_within_limits(batches, "short taps")
        assert_order_preserved(batches, events, "short taps")
        if len(batches[0]) != MAX_INPUT_EVENTS:
            raise Failure(f"the first batch holds {len(batches[0])} events, "
                          f"expected the full {MAX_INPUT_EVENTS}")
        detail(f"{len(batches[0])} events, "
               f"{input_body_bytes(batches[0])} bytes")

    with check("a batch of long taps is limited by the body size"):
        events = combo_taps(200)
        batches = input_batches(events)
        assert_within_limits(batches, "long taps")
        assert_order_preserved(batches, events, "long taps")
        if len(batches[0]) >= MAX_INPUT_EVENTS:
            raise Failure(f"the first batch holds {len(batches[0])} events, "
                          f"which the body limit should have cut short")
        detail(f"{len(batches[0])} events, "
               f"{input_body_bytes(batches[0])} bytes")

    with check("the file browser's 64-keypress field clear fits its requests"):
        # The case that failed: 64 inst_del taps as one request is 4110 bytes.
        events = taps("inst_del", 64)
        if input_body_bytes(events) <= MAX_INPUT_BODY_BYTES:
            raise Failure("64 inst_del taps no longer exceed the body limit, "
                          "so this check no longer covers what it was written "
                          "for")
        batches = input_batches(events)
        assert_within_limits(batches, "field clear")
        assert_order_preserved(batches, events, "field clear")
        detail(f"{len(events)} taps as {[len(b) for b in batches]}")

    with check("one oversized event is passed on rather than dropped"):
        # Nothing can split a single event, so it goes to the device, which
        # answers. Losing it here would be the failure this whole module is
        # about.
        events = [{"kind": "keyboard",
                   "inputs": ["x" * (MAX_INPUT_BODY_BYTES + 100)],
                   "transition": "tap"}]
        batches = input_batches(events)
        assert_order_preserved(batches, events, "oversized event")
        if len(batches) != 1 or len(batches[0]) != 1:
            raise Failure(f"expected one batch of one event, got "
                          f"{[len(b) for b in batches]}")

    with check("no events make no requests"):
        if input_batches([]) != []:
            raise Failure("an empty run of events produced a request")

    suite_ok("input_batching_test", "5 checks")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("input_batching_test", str(exc))
        raise SystemExit(1)
