import dataclasses
import json
import os
import time

import report
import screens
from runtests.device import DEVICE_ERRORS, Device
from runtests.model import Options


def make_output_tree(directory: str) -> bool:
    """Create the -j destination, or report why not and let the run continue.

    Where the run records itself is not part of what the run is testing, so a
    path that cannot be created is a message and a run without JSONL, never a
    traceback before the first suite. It is caught here, before any device is
    touched, so the operator learns of it at the start rather than from an
    empty directory afterwards.
    """
    try:
        os.makedirs(directory, exist_ok=True)
    except OSError as exc:
        report.warn(f"-o {directory} could not be created ({exc}); this run "
                    "keeps no artifacts")
        return False
    return True


# Where an attempt after the first writes the files it would otherwise
# overwrite. Attempt 1 writes exactly where it always did, so a clean run gains
# no directory at all and every path in the layout stays as it was; only a run
# that actually retried something grows one level.
ATTEMPT_DIRECTORY = "attempt-%d"


def attempt_directory(output_dir: str, attempt: int) -> str:
    """Where this attempt's per-attempt files go.

    The per-suite JSONL is deliberately not here. Every record already carries
    `attempt`, the report joins on the `<target>/<label>/<suite>/<attempt>`
    identity key, and the file is truncated on the first attempt and appended
    to afterwards, so it already holds every attempt in order. Splitting it
    would break that rule and the report's key for nothing.

    The console log and the failure capture are here because they are not
    records: they have no attempt field to join on, so a second attempt
    writing them either overwrites the first or appends without a boundary,
    and a reader looking at the capture of a suite that failed twice cannot
    tell which failure it is looking at.
    """
    if attempt <= 1 or not output_dir:
        return output_dir
    return os.path.join(output_dir, ATTEMPT_DIRECTORY % attempt)


# What the capture reads out of the C64's own screen when no menu is open. The
# screen matrix is at $0400 and 1000 bytes is one 40x25 plane, well inside the
# API's own read limit, so it needs no chunking.
SCREEN_RAM = 0x0400
SCREEN_CELLS = 1000


def capture_failure(device: Device, options: Options, mode: str, label: str,
                    suite_name: str, attempt: int) -> None:
    """Write what the device looked like after a suite failed.

    Taken after every suite that exits non-zero, needing no flag beyond
    somewhere to write: on a green run it costs nothing, because no suite
    failed. Three reads, all GET, all on the runner's five-second
    probe client, so a device that has just been taken down cannot make this
    outlast the run's own budget.

    Taken here rather than later because `ui_state.verify` presses RUN/STOP,
    opens the file browser and closes the menu, so by the time the gate has run
    the screen a suite left is gone.

    Nothing here can fail a run. A suite that has just taken the device down is
    exactly the case where these calls will not answer, and a capture that
    propagated would turn a suite failure into a harness failure.
    """
    directory = os.path.join(attempt_directory(options.output_dir, attempt),
                             "capture")
    stem = f"{label}-{suite_name}-{attempt}"
    state: dict[str, object] = {"mode": mode, "source": "", "errors": []}
    screen_text: list[str] = []
    screen_raw = b""

    try:
        os.makedirs(directory, exist_ok=True)
    except OSError as exc:
        report.detail(f"{suite_name}: the capture could not be written: {exc}")
        return

    api = device.probe
    if mode == "telnet":
        # machine:menu_screen returns the first live user interface whose
        # screen is exactly 40x25, and a Telnet session is 60x24, so it answers
        # with the overlay's screen or with 404 and neither is what the suite
        # was looking at. The suite published every screen it read, so the last
        # record before it ended is exactly that, and reading a file the run
        # already wrote costs the device nothing.
        spool = os.path.join(options.output_dir, "screens.jsonl")
        now = time.time()
        record = screens.last_before(spool, now, screens.TELNET,
                                     suite=suite_name, attempt=attempt)
        source = "telnet-spool"
        if record is not None and not any(
                str(row).strip() for row in record.get("text") or []):
            # A session that dropped mid-suite publishes an empty screen last,
            # and an empty block answers nothing. The screen before it is what
            # the suite was working on, and it is named apart so the report
            # does not present it as the screen the suite ended on.
            earlier = screens.last_before(spool, now, screens.TELNET,
                                          suite=suite_name, attempt=attempt,
                                          non_blank=True)
            if earlier is not None:
                record = earlier
                source = "telnet-spool-earlier"
        if record:
            state["source"] = source
            screen_text = [str(row) for row in record.get("text") or []]
        else:
            state["source"] = "unavailable"
            state["errors"].append(
                "no Telnet screen was spooled for this suite, and "
                "machine:menu_screen would answer with a screen nobody drove")
    else:
        try:
            body = api.machine.menu_screen()
            if body is not None:
                state["source"] = "menu_screen"
                screen_raw = body
                screen_text = api.machine.menu_rows()
            else:
                # 404 here means no menu is open rather than old firmware, so
                # this is the ordinary path: most suites leave the menu closed
                # and the interesting screen is the C64's.
                state["source"] = "readmem"
                screen_raw = api.machine.readmem(SCREEN_RAM, SCREEN_CELLS)
                screen_text = c64_screen_rows(screen_raw)
        except DEVICE_ERRORS as exc:
            state["source"] = state["source"] or "unavailable"
            state["errors"].append(f"screen: {report.format_exception(exc)}")

    try:
        state["heap"] = api.machine.heap()
    except DEVICE_ERRORS as exc:
        state["errors"].append(f"heap: {report.format_exception(exc)}")
    try:
        state["drives"] = {slot: dataclasses.asdict(info)
                           for slot, info in api.drives.list().items()}
    except DEVICE_ERRORS as exc:
        state["errors"].append(f"drives: {report.format_exception(exc)}")

    write_capture(directory, stem, screen_text, screen_raw, state)


def c64_screen_rows(data: bytes) -> list[str]:
    """The C64 screen matrix as 25 rows of 40 characters.

    Screen codes, not the literal ASCII the menu plane carries, and the two
    must not share a decode path. In the unshifted set code 0 is `@`, 1 to 26
    are the letters, 27 to 31 are `[`, a pound sign, `]`, an up arrow and a
    left arrow, and 32 to 63 are the ASCII characters of the same value.
    Everything from 64 up is a graphics character with no text form, so it
    reads as a space rather than as the letter its byte value would give in
    ASCII. Bit 7 is reverse video and is masked off, as
    `api.MachineApi.menu_rows` masks it for the other screen.

    Best effort, and the report says so: the matrix moves with the VIC bank and
    with $D018, so a program that has moved it is not rendered here.
    """
    high = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\u00a3]\u2191\u2190"
    text = "".join(high[byte] if byte < 0x20
                   else (chr(byte) if byte < 0x40 else " ")
                   for byte in (b & 0x7F for b in data[:SCREEN_CELLS]))
    return [text[row * 40:(row + 1) * 40] for row in range(SCREEN_CELLS // 40)]


def write_capture(directory: str, stem: str, text: list[str], raw: bytes,
                  state: dict[str, object]) -> None:
    """The three artefacts, named so the report finds them with no lookup table."""
    try:
        with open(os.path.join(directory, f"{stem}-screen.txt"), "w",
                  encoding="utf-8") as handle:
            handle.write(report.masked("\n".join(text)) + ("\n" if text else ""))
        if raw:
            with open(os.path.join(directory, f"{stem}-screen.bin"), "wb") as handle:
                handle.write(raw)
        # Serialised before it is opened, so a value json cannot encode leaves
        # no half-written file behind for the artifact to carry.
        body = json.dumps(report.masked(state), indent=2, sort_keys=True) + "\n"
        with open(os.path.join(directory, f"{stem}-state.json"), "w",
                  encoding="utf-8") as handle:
            handle.write(body)
    except (OSError, TypeError, ValueError) as exc:
        report.detail(f"the capture could not be written: {exc}")
