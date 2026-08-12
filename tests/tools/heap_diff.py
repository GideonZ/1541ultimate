#!/usr/bin/env python3
"""Find which allocation sites leak, by running the same work twice.

A live block is not a leaked block. The device's own report says what is
outstanding right now, which on any real firmware is mostly legitimate: the
directory on screen, the current session, cached structures. Reading it once
and treating the big entries as leaks is how you end up fixing the wrong thing.

So run the work twice and compare. A caller whose block count is the same after
one round and after two is holding live objects. A caller whose count grows by
the same amount each round is leaking that much per round. That difference is
the whole tool.

Needs firmware built with EXTRA_DEFINES=-DHEAP_TRACK=1; without it the
endpoints are absent and this exits saying so.

    tests/tools/heap_diff.py -H 192.168.1.64 -- ./run-tests -H 192.168.1.64 -s prg-context-menu

Whatever follows -- is run once per round. Give it something that does the same
work every time, or the comparison means nothing.
"""

import argparse
import os
import re
import subprocess
import sys
import time

# tests/lib holds the one shared REST client; every suite and tool goes through
# it so the retry policy lives in exactly one place.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import rest as rest_lib  # noqa: E402  (needs tests/lib on sys.path first)

BLOCKS = "/v1/machine:heapblocks"
RESET = "/v1/machine:heapblocks_reset"
HEAP = "/v1/machine:heap"


def callers(rest):
    """Map caller address -> (blocks, bytes), plus the raw report."""
    data = rest.json(BLOCKS)
    out = {}
    entries = data.get("caller", [])
    if isinstance(entries, str):
        entries = [entries]
    for line in entries:
        m = re.match(r"(\d+) blocks (\d+) bytes ra ([0-9A-Fa-f]+)", line)
        if m:
            out[m.group(3)] = (int(m.group(1)), int(m.group(2)))
    return out, data


def free_heap(rest):
    return int(rest.json(HEAP)["free"])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-H", "--host", required=True)
    ap.add_argument("-p", "--password", default=os.environ.get("U64_PASS"))
    ap.add_argument("-t", "--timeout", type=float, default=30.0)
    ap.add_argument("-n", "--rounds", type=int, default=2,
                    help="rounds to run; 2 is enough to separate live from leaked (default: 2)")
    ap.add_argument("-s", "--settle", type=float, default=8.0,
                    help="seconds to settle before each sample (default: 8.0)")
    ap.add_argument("--warmup", action="store_true",
                    help="run the command once before measuring, so one-time "
                         "costs are not counted as leaks")
    ap.add_argument("command", nargs=argparse.REMAINDER,
                    help="after --, the command to run each round")
    args = ap.parse_args()

    cmd = args.command[1:] if args.command and args.command[0] == "--" else args.command
    if not cmd:
        ap.error("give a command to run after --")

    rest = rest_lib.RestClient(args.host, args.password or None, args.timeout)

    if rest.status("GET", BLOCKS) != 200:
        print("This firmware has no machine:heapblocks. Rebuild with "
              "EXTRA_DEFINES=-DHEAP_TRACK=1", file=sys.stderr)
        return 2

    if args.warmup:
        print("warmup round (not measured)")
        subprocess.run(cmd, check=False)
        time.sleep(args.settle)

    rest.expect("PUT", RESET)
    before_free = free_heap(rest)
    rounds = []
    for i in range(args.rounds):
        print(f"round {i + 1}/{args.rounds}: {' '.join(cmd)}")
        result = subprocess.run(cmd, check=False)
        if result.returncode != 0:
            print(f"  command exited {result.returncode}; a failed round makes "
                  f"the comparison meaningless", file=sys.stderr)
            return 1
        time.sleep(args.settle)
        rounds.append(callers(rest)[0])

    after_free = free_heap(rest)
    first, last = rounds[0], rounds[-1]
    spent = before_free - after_free
    span = len(rounds) - 1 or 1

    print(f"\nfree heap: {before_free} -> {after_free} = {spent} bytes over "
          f"{args.rounds} rounds")
    print(f"\n{'caller':>10}  {'blocks/round':>13}  {'bytes/round':>12}  verdict")
    growing = []
    for ra in sorted(set(first) | set(last)):
        b0, y0 = first.get(ra, (0, 0))
        b1, y1 = last.get(ra, (0, 0))
        db, dy = (b1 - b0) / span, (y1 - y0) / span
        if db > 0:
            growing.append((dy, ra, db))
        verdict = "GROWING" if db > 0 else "steady"
        if db > 0 or b1:
            print(f"{ra:>10}  {db:13.1f}  {dy:12.1f}  {verdict}")

    if growing:
        growing.sort(reverse=True)
        print("\nLeaking, largest first. Resolve with:")
        print("  nios2-elf-addr2line -f -C -e <ultimate.elf> 0x<addr>")
        for dy, ra, db in growing:
            print(f"  0x{ra}  {dy:.0f} bytes/round in {db:.1f} blocks")
    else:
        print("\nNothing grew between rounds.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
