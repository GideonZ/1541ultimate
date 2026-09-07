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
from collections.abc import Sequence

from report import Failure

# Stores whose values a run must not put back. See the module docstring.
VOLATILE_STORES = ("Clock Settings",)

# A config read is idempotent and the device answers it empty while it is
# busy, so a store that comes back without items is asked for again rather
# than believed. Same rule as tests/e2e/lib/temp_settings.py applies to the
# two items it manages.
READ_ATTEMPTS = 3
READ_PAUSE_SECONDS = 0.5


class Unreadable(Failure):
    """A store this machine would not describe. Ends the pass it happened in.

    A device that accepts a connection and then never answers is a shape this
    bench sees, and every read against it costs the client's whole timeout,
    three times over inside rest.py and three times again here. Walking the
    remaining stores would turn one stalled device into an hour of waiting
    before the first suite, so the first store that will not answer ends the
    capture and the restore alike.
    """


@dataclass(frozen=True)
class Change:
    """One item whose value is not what it was when the snapshot was taken."""

    machine: str
    store: str
    item: str
    was: object
    now: object

    def __str__(self) -> str:
        # A password item's value really is served in clear
        # (software/api/route_configs.cc emits CFG_TYPE_STRPASS as a string),
        # and this line reaches the run log and the generated report.
        if self.item.lower().endswith("password"):
            return f"{self.machine}: {self.store} / {self.item} (not shown)"
        return (f"{self.machine}: {self.store} / {self.item} "
                f"{self.now!r} -> {self.was!r}")


@dataclass(frozen=True)
class Snapshot:
    """What one machine's settings were at a moment in time."""

    machine: str
    # store -> item -> value, for every store this machine serves and this
    # module does not skip.
    settings: dict[str, dict[str, object]]

    @property
    def item_count(self) -> int:
        return sum(len(items) for items in self.settings.values())

    def changes(self, api) -> list[Change]:
        """Every captured item whose value differs now, read store by store.

        Raises `Unreadable` at the first store the device will not describe.
        Nothing is known about that store, so a caller writing values back on
        that basis would be guessing, and a device that has stopped answering
        will not describe the rest either.
        """
        found: list[Change] = []
        for store, items in self.settings.items():
            current = _read_store(api, store)
            if current is None:
                raise Unreadable(f"{self.machine}: {store} could not be read")
            for item, was in items.items():
                now = current.get(item)
                if item in current and now != was:
                    found.append(Change(self.machine, store, item, was, now))
        return found

    def restore(self, api) -> tuple[list[Change], list[tuple[Change, str]]]:
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
        refused: list[tuple[Change, str]] = []
        written: list[Change] = []
        for change in wanted:
            try:
                api.configs.set(change.store, change.item, change.was)
            except Failure as exc:
                refused.append((change, str(exc)))
            else:
                written.append(change)
        # One read per store touched, rather than one per item written.
        confirmed: list[Change] = []
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
    pointed at it. The first store the device will not describe raises
    `Unreadable`; see that class for why the rest are not tried.
    """
    try:
        stores = api.configs.category_names()
    except Failure as exc:
        raise Failure(f"{machine}: the settings could not be listed: {exc}") from exc
    settings: dict[str, dict[str, object]] = {}
    for store in stores:
        if store in skip:
            continue
        items = _read_store(api, store)
        if items is None:
            raise Unreadable(f"{machine}: {store} could not be read after "
                             f"{READ_ATTEMPTS} attempts")
        settings[store] = items
    return Snapshot(machine=machine, settings=settings)


def _read_store(api, store: str) -> dict[str, object] | None:
    """One store's items and values, or None when it would not answer.

    Retried, because the device answers a config read empty while it is busy
    and an empty answer here would read as "this store has no settings".
    """
    for attempt in range(READ_ATTEMPTS):
        try:
            items = api.configs.category(store)
        except Failure:
            items = None
        if items:
            return dict(items)
        if attempt + 1 < READ_ATTEMPTS:
            time.sleep(READ_PAUSE_SECONDS)
    return None
