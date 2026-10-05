"""Which cartridge is plugged into which computer, found by asking the machines.

A cartridge in a computer shares that computer's IEC bus, keyboard and RAM, so
the runner has to know the pairing before it schedules anything: two targets
that share hardware cannot run at the same time, and each side's drives have to
be kept off the bus the other one is testing.

The pairing comes from two places, and neither needs a variable to be set:

- A target written as `cartridge@computer` states it.
- A cartridge named on its own beside other hosts is tested against each of
  them. The computer writes a random fingerprint into its own RAM, and the
  cartridge reads that address through its own DMA. Only a cartridge that is
  plugged into that computer sees the fingerprint, and the chance of a
  coincidence is 2**-64.

The result is exported through `U64_COMPUTERS`, which every suite process and
library already reads, so the pairing is one fact in one place. A value the
operator exported is kept: an explicit statement outranks a measurement.
"""

from __future__ import annotations

import concurrent.futures
import dataclasses
import json
import os
import secrets
import socket
import urllib.request
from collections.abc import Callable, Sequence

import machine as machine_lib
import targets

# $0340 is in the cassette buffer, which nothing uses while a run is going.
FINGERPRINT_ADDRESS = 0x0340
FINGERPRINT_LENGTH = 8

# Set once the cabling of a run is settled, so the suite processes the runner
# starts take its answer instead of fingerprinting machines that are busy.
SETTLED_ENV = "U64_CABLING_SETTLED"

Pair = tuple[str, str]
Reader = Callable[[str, int, int], bytes]
Writer = Callable[[str, int, bytes], None]


def _own_machine(host: str) -> targets.Target:
    """`host` as a machine of its own, whatever U64_COMPUTERS says about it.

    A cartridge named from the environment resolves to its computer, which
    would make the fingerprint read come from the machine that wrote it.
    """
    return dataclasses.replace(targets.parse(host), computer=host)


def _api(host: str, password: str | None, timeout: float):
    from api import UltimateApi
    return UltimateApi(_own_machine(host), password, timeout)


def product_kind(host: str, password: str | None, timeout: float) -> str | None:
    """The machine kind `host` reports, or None when it cannot be asked."""
    try:
        return machine_lib.classify(_api(host, password, timeout).info().product).kind
    except Exception:       # noqa: BLE001  unreachable or unknown: nothing to pair
        return None


def plugged_into(cartridge: str, computer: str, read: Reader, write: Writer,
                 token: Callable[[int], bytes] = secrets.token_bytes) -> bool:
    """Whether `cartridge` sits in `computer`.

    The fingerprint differs from what the address held, so a computer whose
    RAM already carries the same bytes cannot produce a match by itself. The
    address is put back either way.
    """
    original = read(computer, FINGERPRINT_ADDRESS, FINGERPRINT_LENGTH)
    fingerprint = token(FINGERPRINT_LENGTH)
    while fingerprint == original:
        fingerprint = token(FINGERPRINT_LENGTH)
    write(computer, FINGERPRINT_ADDRESS, fingerprint)
    try:
        try:
            seen = read(cartridge, FINGERPRINT_ADDRESS, FINGERPRINT_LENGTH)
        except Exception:   # noqa: BLE001  a device that cannot read is not in it
            return False
        return seen == fingerprint
    finally:
        write(computer, FINGERPRINT_ADDRESS, original)


def detect(hosts: Sequence[str], password: str | None, timeout: float,
           kind_of: Callable[[str], str | None] | None = None,
           read: Reader | None = None, write: Writer | None = None) -> list[Pair]:
    """The (cartridge, computer) pairs among `hosts`, by fingerprint.

    Only a host whose product is an Ultimate II can be a cartridge, and only
    another kind of host can be its computer, so a run of one machine, or of
    machines that cannot be plugged into each other, touches no RAM at all.
    """
    kind_of = kind_of or (lambda host: product_kind(host, password, timeout))
    read = read or (lambda host, address, length:
                    _api(host, password, timeout).machine.readmem(address, length))
    write = write or (lambda host, address, data:
                      _api(host, password, timeout).machine.writemem(address, data))
    kinds = {host: kind_of(host) for host in hosts}
    cartridges = [h for h, k in kinds.items() if k == machine_lib.U2]
    computers = [h for h, k in kinds.items() if k not in (None, machine_lib.U2)]
    found: list[Pair] = []
    for cartridge in cartridges:
        for computer in computers:
            if plugged_into(cartridge, computer, read, write):
                found.append((cartridge, computer))
                break
    return found


SWEEP_TIMEOUT_SECONDS = 0.6


def _info_product(address: str) -> str | None:
    """The `product` of whatever answers /v1/info at `address`, or None."""
    try:
        with urllib.request.urlopen(f"http://{targets.device_of(address)}/v1/info",
                                    timeout=SWEEP_TIMEOUT_SECONDS) as answer:
            return str(json.load(answer).get("product", ""))
    except Exception:       # noqa: BLE001  nothing, or something else, answers
        return None


def sweep_cartridges(computer: str,
                     product_at: Callable[[str], str | None] = _info_product,
                     resolve: Callable[[str], str] = socket.gethostbyname) -> list[str]:
    """Addresses on `computer`'s /24 that answer as an Ultimate II.

    A cartridge nobody named on the command line is still on the computer's
    bus, so the runner looks for it rather than trusting that there is none.
    """
    try:
        own = resolve(computer)
    except OSError:
        return []
    prefix = own.rsplit(".", 1)[0]
    candidates = [f"{prefix}.{n}" for n in range(1, 255) if f"{prefix}.{n}" != own]
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as pool:
        products = list(pool.map(product_at, candidates))
    return [address for address, product in zip(candidates, products)
            if product and "ultimate ii" in product.lower()]


def declare(pairs: Sequence[Pair]) -> list[Pair]:
    """Add `pairs` to U64_COMPUTERS, keeping any cartridge it already names.

    Answers the pairs that were added.
    """
    entries = [e.strip() for e in (os.environ.get(targets.COMPUTERS_ENV) or "").split(",")
               if e.strip()]
    named = {e.partition(targets.SEPARATOR)[0].lower() for e in entries}
    added = []
    for cartridge, computer in pairs:
        if cartridge.lower() in named:
            continue
        entries.append(f"{cartridge}{targets.SEPARATOR}{computer}")
        named.add(cartridge.lower())
        added.append((cartridge, computer))
    if added:
        os.environ[targets.COMPUTERS_ENV] = ",".join(entries)
    return added


def establish(tokens: Sequence[str], password: str | None, timeout: float,
              **seams) -> list[Pair]:
    """Settle the cabling for a run of `tokens`, and declare what was learned.

    Answers the pairs that were added to the declaration, for the caller to
    report. Explicit `cartridge@computer` tokens come first; the bare hosts
    that are not already paired are then checked against one another.
    """
    if os.environ.get(SETTLED_ENV):
        return []
    os.environ[SETTLED_ENV] = "1"
    sweep = seams.pop("sweep", sweep_cartridges)
    # A cartridge also named on its own is being tested as a device of its own
    # as well, so the pair is not stated for it: declaring it would turn the
    # bare token into the same target as the split one.
    bare = {t.lower() for t in tokens if targets.SEPARATOR not in t}
    explicit = [tuple(t.split(targets.SEPARATOR, 1)) for t in tokens
                if targets.SEPARATOR in t and t.split(targets.SEPARATOR, 1)[0].lower() not in bare]
    added = declare([(c, m) for c, m in explicit])
    unpaired = [t for t in tokens if targets.SEPARATOR not in t
                and not targets.parse(t).split]
    if len(unpaired) > 1:
        added += declare(detect(unpaired, password, timeout, **seams))
    # A computer with no cartridge declared may still have one fitted that the
    # run does not name, and it would share the computer's IEC bus.
    kind_of = seams.get("kind_of") or (lambda host: product_kind(host, password, timeout))
    for computer in (t.partition(targets.SEPARATOR)[2] or t for t in tokens):
        if kind_of(computer) in (None, machine_lib.U2) or targets.declared_cartridges(computer):
            continue
        for address in sweep(computer):
            added += declare(detect([address, computer], password, timeout, **seams))
    return added
