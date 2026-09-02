"""How much of the suite tree a run covers, in one vocabulary.

Two layers, each using the name the wider world already uses for it.

*Tags* are what a scenario declares about itself, the way JUnit 5's `@Tag`,
TestNG's groups, NUnit's categories, xUnit's traits, pytest's markers and
CTest's labels all do. A tag says what a scenario costs or risks, never when
it should run.

*Profiles* are what a person selects when starting a run, the way Maven's `-P`
and Spring's `@Profile` do: a named bundle of configuration. A profile decides
which tags run, which UI modes are swept, and whether the suites kept out of an
ordinary run are included.

The two are deliberately separate. A scenario that knows it is slow can say so
once, and every profile decides for itself what to do with that, so adding a
profile edits this file and nothing else.

Profiles are ordered and cumulative: everything `quick` runs, `standard` runs
too. That is what lets a scenario name the shallowest profile it belongs to
rather than list every profile it appears in.

    smoke        1 min    after a deploy, a reflash, or a recovery
    quick        5 min    the default, before pushing
    standard     15 min   the merge gate, and CI on a pull request
    deep         60 min   nightly
    exhaustive   90 min   before a release, or chasing a ghost

Those are measured rather than intended, from a full Ultimate 64 run on
2026-09-02: every suite this file selects, at its own recorded duration,
multiplied by the transports the profile sweeps. They move when the tree does,
so treat them as the shape of the ladder rather than as a promise: what a run
actually cost is in its own summary, and `run-tests --list-profiles --measured`
reads it back out of recorded runs.

A suite or scenario declares the shallowest profile that includes it:

    if profiles.skip_below(profiles.STANDARD, LABEL):
        return

which reports the check as SKIP naming the profile it needs, in the same shape
`machine.skip_without_fix` reports a missing firmware fix.
"""

from __future__ import annotations

import os
from typing import Optional, Sequence, Tuple

from report import check_skip, check_start

SMOKE = "smoke"
QUICK = "quick"
STANDARD = "standard"
DEEP = "deep"
EXHAUSTIVE = "exhaustive"

# In order, shallowest first. Membership is cumulative along this list.
ORDER: Tuple[str, ...] = (SMOKE, QUICK, STANDARD, DEEP, EXHAUSTIVE)

# What a bare `run-tests` does. Deliberately not the deepest: the default is
# the one a person runs before pushing, and a default that takes twenty minutes
# is a default that gets skipped.
DEFAULT = QUICK

# Read at import, because run-tests starts each suite as its own process and a
# flag it parsed cannot reach them any other way. Same convention as
# machine.ASSUME_ENV and report.E2E_SUITE.
ENV = "E2E_PROFILE"

# Which UI transports a profile sweeps when --mode is not given. The matrix is
# the largest lever there is: sweeping all three triples every suite that
# drives the menu.
MODES = {
    SMOKE: ("overlay",),
    QUICK: ("overlay",),
    STANDARD: ("overlay",),
    # Two transports rather than three: a second pass over the UI suites is
    # what catches a transport-specific defect, and the third costs as much
    # again for the narrowest return. Measured on an Ultimate 64, one overlay
    # pass over every suite is about 31 minutes.
    DEEP: ("overlay", "freeze"),
    EXHAUSTIVE: ("overlay", "freeze", "telnet"),
}

# The profiles that also run the suites an ordinary run leaves out, the ones
# registered `manual=True` because they need an operator decision, extra
# privileges, a long wall-clock wait, or a device setting they change.
INCLUDES_MANUAL = (DEEP, EXHAUSTIVE)


class UnknownProfile(ValueError):
    """A profile name that is not in ORDER. The message lists the real ones."""


def parse(name: str) -> str:
    """One profile name, checked against ORDER."""
    wanted = (name or "").strip().lower()
    if wanted not in ORDER:
        raise UnknownProfile(
            f"unknown profile {name!r}; the profiles are "
            f"{', '.join(ORDER)}")
    return wanted


def current() -> str:
    """The profile this process is running under."""
    raw = (os.environ.get(ENV) or "").strip().lower()
    return raw if raw in ORDER else DEFAULT


def rank(name: str) -> int:
    return ORDER.index(parse(name))


def includes(needed: str, selected: Optional[str] = None) -> bool:
    """Whether `selected` is deep enough to run something tagged `needed`."""
    return rank(selected or current()) >= rank(needed)


def modes_for(name: str) -> Sequence[str]:
    """The UI transports this profile sweeps when none was named."""
    return MODES[parse(name)]


def includes_manual(name: str) -> bool:
    return parse(name) in INCLUDES_MANUAL


def missing(needed: str, selected: Optional[str] = None) -> str:
    """Why this profile does not run something tagged `needed`, or ""."""
    if includes(needed, selected):
        return ""
    return (f"runs from the {needed} profile up; this run is "
            f"{selected or current()}")


def skip_below(needed: str, label: str, selected: Optional[str] = None) -> bool:
    """Report `label` as skipped, and answer True, when the profile is shallower.

    The one line a tagged check needs, and the caller returns on True:

        if profiles.skip_below(profiles.STANDARD, LABEL):
            return
    """
    reason = missing(needed, selected)
    if not reason:
        return False
    check_start(label)
    check_skip(reason)
    return True
