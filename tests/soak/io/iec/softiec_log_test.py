#!/usr/bin/env python3
# Device-free checks of the SoftIEC soak's log correlation.

"""Verify softiec_log's correlation against lines the device really wrote.

The soak compares the device's log with what the C64 did, and a wrong verdict
there reads as a firmware defect. The cases here use lines taken from runs on
the bench, so what is checked is the correlation of those lines, not of lines
written to suit it.
"""

import os
import sys
import tempfile
import time
import types
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import softiec_log  # noqa: E402
from report import check, suite_ok  # noqa: E402
from selftest import expect  # noqa: E402
import softiec_soak_test as soak  # noqa: E402

# The stress phase's REST lane reset the drive while a C64 step was between a command and
# its status read (Ultimate II+L, 2026-09-22). The drive logged the command's own answer,
# 72; the C64 read the 73 the reset leaves; logging was off.
RESET_LINE = ('SoftIEC: command failed dev=11 chan=15 part=1 dir="/SOAK020BB2/WORK/" len=5 '
              'txt="Pi\\xFF\\xFF\\x01" -> 72,DISK FULL,00,00 #22')
# The same, with the reset sent while the C64's status read was already under way
# (Ultimate 64 Elite, UCI KERNAL, 2026-09-24).
READ_RESET_LINE = ('SoftIEC: command failed dev=11 chan=15 part=1 dir="/SOAKBD666B/WORK/" len=23 '
                   'txt="C:CAT2=SRC0,SRC1,SRC2,S" -> 62,FILE NOT FOUND,00,00 #802')


def main() -> int:
    cli.device_free_arguments(__doc__)

    with check("a command whose status read crossed a drive reset pairs with the line it wrote"):
        event = softiec_log.Event(op="command", chan=15, txt=b"Pi\xff\xff\x01", status=73,
                                  t0=10.0, t1=12.0)
        events = softiec_log.across_resets([event], [(11.0, 11.5)])
        result = softiec_log.correlate(events, [RESET_LINE], logging_on=False, label="reset")
        expect("unexpected well-formed lines", result.unexpected_bad, [])
        expect("matched", result.matched, 1)

    with check("a reset outside a command's span leaves its status in force"):
        event = softiec_log.Event(op="command", chan=15, txt=b"Pi\xff\xff\x01", status=73,
                                  t0=10.0, t1=12.0)
        events = softiec_log.across_resets([event], [(12.5, 13.0), (8.0, 9.5)])
        expect("status", events[0].status, 73)
        expect("optional", events[0].optional, False)

    with check("a drive reset sent during the status read makes the command optional"):
        session = types.SimpleNamespace(recording=True, log_events=[], drive_resets=[])

        class ResetDuringStatusRead:
            def call(self, op, **_kwargs):
                if op != soak.READ_TO_EOI:
                    return b""
                time.sleep(0.01)
                sent = time.monotonic()
                session.drive_resets.append((sent, time.monotonic()))
                time.sleep(0.01)
                return b"73,U64HD ULTIMATE DOS V2.0,00,00"

        agent = soak.RecordingAgent(ResetDuringStatusRead(), session)
        agent.call(soak.WRITE, 15, b"C:CAT2=SRC0,SRC1,SRC2,S")
        agent.call(soak.READ_TO_EOI, 15)
        events = softiec_log.across_resets(session.log_events, session.drive_resets)
        result = softiec_log.correlate(events, [READ_RESET_LINE], logging_on=False, label="reset")
        expect("unexpected well-formed lines", result.unexpected_bad, [])
        expect("matched", result.matched, 1)

    with check("the run's collected log gives this device's whole lines after the mark"):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "syslog.txt")
            with open(path, "w", encoding="utf-8") as out:
                out.write("1758700000.100 before the mark\n")
            source = softiec_log.CollectedLogSource(path, "192.168.1.74")
            source.mark()
            with open(path, "a", encoding="utf-8") as out:
                # The collector's own format, then a line it has not finished writing.
                out.write(f"1758700001.250 {READ_RESET_LINE}\n1758700001.300 SoftIEC: com")
            expect("entries", source.entries(), [("192.168.1.74", READ_RESET_LINE)])

    suite_ok("softiec_log_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
