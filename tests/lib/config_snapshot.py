"""Every setting a machine serves, and putting them back the way they were.

A run changes settings. Some suites do it on purpose, and a suite that fails
part way through can leave one changed by accident, so the machine the next
run starts against is not the machine this one started against. That makes a
failure depend on what ran before it, which is the hardest kind to reproduce.

The snapshot is read from `GET /v1/configs` and one `GET /v1/configs/<store>`
per store, which is 15 to 23 requests on the machines here (159 to 220
items). Restoring writes only the items whose value differs from the
snapshot, so a run that changed nothing writes nothing.

`PUT /v1/configs/<store>/<item>` sets the value in RAM and calls the store's
`at_close_config()` to make it take effect; it does not write flash
(software/api/route_configs.cc). Both the capture and the restore therefore
describe the running configuration, and neither changes what the machine
boots with. A suite that saved a setting to flash itself has changed that, and
putting the running value back does not undo it.

Clock Settings is left alone. It is a real-time clock rather than a
preference, so its values are supposed to differ at the end of a run, and
writing the captured ones back would set the clock backwards.
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

from report import Failure

# Stores whose values a run must not put back. See the module docstring.
VOLATILE_STORES = ("Clock Settings",)

# A config read is idempotent and the device answers it empty while it is
# busy, so a store that comes back without items is asked for again rather
# than believed. Same rule as tests/e2e/lib/temp_settings.py applies to the
# two items it manages.
READ_ATTEMPTS = 3
READ_PAUSE_SECONDS = 0.5


@dataclass(frozen=True)
class Change:
    """One item whose value is not what it was when the snapshot was taken."""

    machine: str
    store: str
    item: str
    was: object
    now: object

    def __str__(self) -> str:
        return (f"{self.machine}: {self.store} / {self.item} "
                f"{self.now!r} -> {self.was!r}")


@dataclass(frozen=True)
class Snapshot:
    """What one machine's settings were at a moment in time."""

    machine: str
    # store -> item -> value, holding only the stores that could be read.
    settings: Dict[str, Dict[str, object]]
    # Stores the device listed but would not describe, and why.
    unread: Tuple[Tuple[str, str], ...] = ()

    @property
    def item_count(self) -> int:
        return sum(len(items) for items in self.settings.values())

    def changes(self, api) -> List[Change]:
        """Every captured item whose value differs now, read store by store.

        A store that cannot be read now is not reported as changed: nothing is
        known about it, and a caller writing values back on that basis would
        be guessing.
        """
        found: List[Change] = []
        for store, items in self.settings.items():
            current = _read_store(api, store)
            if current is None:
                continue
            for item, was in items.items():
                now = current.get(item)
                if item in current and now != was:
                    found.append(Change(self.machine, store, item, was, now))
        return found

    def restore(self, api) -> Tuple[List[Change], List[Tuple[Change, str]]]:
        """Put back every item that differs. Answers what was done.

        Returns (restored, refused): the changes whose captured value the
        device took and confirmed, and the ones it did not, each with the
        reason. Confirmation is a re-read of the stores that were written, so
        a device that answers HTTP 200 and keeps the old value is reported as
        a refusal rather than as a success.
        """
        wanted = self.changes(api)
        if not wanted:
            return [], []
        refused: List[Tuple[Change, str]] = []
        written: List[Change] = []
        for change in wanted:
            try:
                api.configs.set(change.store, change.item, change.was)
            except Failure as exc:
                refused.append((change, str(exc)))
            else:
                written.append(change)
        # One read per store touched, rather than one per item written.
        confirmed: List[Change] = []
        for store in sorted({change.store for change in written}):
            current = _read_store(api, store)
            for change in [c for c in written if c.store == store]:
                if current is None:
                    refused.append((change, "the store could not be read back"))
                elif current.get(change.item) != change.was:
                    refused.append(
                        (change, f"kept {current.get(change.item)!r}"))
                else:
                    confirmed.append(change)
        return confirmed, refused


def capture(machine: str, api, skip: Sequence[str] = VOLATILE_STORES) -> Snapshot:
    """Read every setting `machine` serves, skipping the volatile stores.

    `machine` names the device for a message; `api` is the `UltimateApi`
    pointed at it. A store the device lists but will not describe is recorded
    in `unread` rather than raising, because a settings read that fails is not
    a reason to abandon a hardware run.
    """
    try:
        stores = api.configs.category_names()
    except Failure as exc:
        raise Failure(f"{machine}: the settings could not be listed: {exc}") from exc
    settings: Dict[str, Dict[str, object]] = {}
    unread: List[Tuple[str, str]] = []
    for store in stores:
        if store in skip:
            continue
        try:
            items = _read_store(api, store, raising=True)
        except Failure as exc:
            unread.append((store, str(exc)))
            continue
        if items is None:
            unread.append((store, "answered no items"))
            continue
        settings[store] = items
    return Snapshot(machine=machine, settings=settings, unread=tuple(unread))


def _read_store(api, store: str, raising: bool = False) -> Optional[Dict[str, object]]:
    """One store's items and values, or None when it would not answer.

    Retried, because the device answers a config read empty while it is busy
    and an empty answer here would read as "this store has no settings".
    """
    last: Optional[Failure] = None
    for attempt in range(READ_ATTEMPTS):
        try:
            items = api.configs.category(store)
        except Failure as exc:
            last = exc
            items = None
        if items:
            return dict(items)
        if attempt + 1 < READ_ATTEMPTS:
            time.sleep(READ_PAUSE_SECONDS)
    if raising and last is not None:
        raise last
    return None
