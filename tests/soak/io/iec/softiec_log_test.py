#!/usr/bin/env python3
# Device-free checks of the SoftIEC soak's log correlation.

"""Verify softiec_log's correlation against lines the device really wrote.

The soak compares the device's log with what the C64 did, and a wrong verdict
there reads as a firmware defect. The cases here use lines taken from runs on
the bench, so what is checked is the correlation of those lines, not of lines
written to suit it.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import softiec_log  # noqa: E402
from report import check, suite_ok  # noqa: E402
from selftest import expect  # noqa: E402

# The stress phase's REST lane reset the drive while a C64 step was between a command and
# its status read (Ultimate II+L, 2026-09-22). The drive logged the command's own answer,
# 72; the C64 read the 73 the reset leaves; logging was off.
RESET_LINE = ('SoftIEC: command failed dev=11 chan=15 part=1 dir="/SOAK020BB2/WORK/" len=5 '
              'txt="Pi\\xFF\\xFF\\x01" -> 72,DISK FULL,00,00 #22')


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

    suite_ok("softiec_log_test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
