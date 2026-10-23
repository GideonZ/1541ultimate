#!/usr/bin/env python3
# Gate check: how uci-net-route ends when its preconditions are not met, without a device.

"""Verify how net_route_test.py ends when its preconditions are not met, with no device.

Two cases. A link that is down, and a command interface that is not there.

An address stays configured when its link goes down (a static address is kept, lwIP's
netif_set_down() does not clear it), so two addresses on the subnet do not mean two
working routes. The suite reads link state from the interface rows of the root menu,
and these cases hold it to skipping on a retained WiFi address with WiFi down, a
retained Ethernet address with Ethernet down, rows it cannot find, and a menu that will
not open.

Absent interface: a C64 Ultimate reads the "Command Interface" setting as Enabled while the whole
register window at $DF1B-$DF1F reads $FF, because nothing answers there. The
suite then has to end as skipped, with the setting put back, and without trying
to bring an interface to Idle that does not exist: release() reads $FF as Data
More and can never succeed, so a cleanup that tried turned the skip into a
failure after a wait for nothing.

The device is a DeviceDouble whose memory reads $FF, driven through the real
net_route_test.main(), the real Uci and the real report lines.
"""

import contextlib
import ipaddress
import io
import os
import sys
import time
from pathlib import Path
from unittest import mock

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401

import net_route_test  # noqa: E402
import selftest  # noqa: E402
from device_double import DeviceDouble  # noqa: E402
from report import Failure, check, suite_fail, suite_ok  # noqa: E402

SUITE = "net_route_precondition_test"
CATEGORY = net_route_test.CONFIG_CATEGORY
SETTING = net_route_test.CFG_CMD_IF
# release() waits this long for Idle, so a run that tried it on the absent
# interface takes at least this much longer than one that does not.
RELEASE_WAIT_SECONDS = 2.0


class AbsentInterface(DeviceDouble):
    """Every readmem answers $FF: the bus floating where the registers should be."""

    def _readmem(self, params):
        return 200, bytes([0xFF]) * int(params.get("length", "256")), "application/octet-stream"


def run_suite(double: DeviceDouble) -> tuple[int, str, float]:
    """net_route_test.main() against `double`: (exit code, what it printed, seconds)."""
    argv = ["net_route_test.py", "-H", "127.0.0.1", "-t", "5", "-b", "1"]
    out = io.StringIO()
    started = time.monotonic()
    with (contextlib.redirect_stdout(out), contextlib.redirect_stderr(out),
          mock.patch.dict(os.environ, double.environment()),
          mock.patch.object(sys, "argv", argv)):
        code = net_route_test.main()
    return code, out.getvalue(), time.monotonic() - started


WIRED = ipaddress.IPv4Address("10.43.23.81")
WIFI = ipaddress.IPv4Address("10.43.23.83")
MAC = "02:15:41:68:80:04"
ROOT = ["Flash   Flash Disk             Ready",
        "Temp    RAM Disk               Ready",
        "USB0    SanDisk 3.2Gen1        Ready"]
NET_UP = f"Net0    IP: {WIRED}       Link Up"
NET_DOWN = f"Net0    MAC {MAC}  Link Down"
WIFI_UP = f"WiFi    IP: {WIFI}       Link Up"
WIFI_DOWN = f"WiFi    MAC {MAC}  Link Down"


def problem(*interface_rows: str) -> str:
    return net_route_test.link_problem(ROOT + list(interface_rows), WIRED, [WIFI])


def case_links_both_up() -> None:
    selftest.expect("both up", problem(NET_UP, WIFI_UP), "")


def case_wifi_down_with_its_address_kept() -> None:
    reason = problem(NET_UP, WIFI_DOWN)
    selftest.expect("WiFi down is named", "WiFi link is down" in reason, True)


def case_ethernet_down_with_its_address_kept() -> None:
    reason = problem(NET_DOWN, WIFI_UP)
    selftest.expect("Ethernet down is named", "Ethernet link is down" in reason, True)


def case_wifi_not_connected_has_no_link_row() -> None:
    reason = problem(NET_UP, "WiFi    Disabled                       Disabled")
    selftest.expect("an interface without a Link Up row", "no menu row shows" in reason, True)


def case_no_interface_rows() -> None:
    reason = problem()
    selftest.expect("rows missing", "cannot be read" in reason, True)


def read_rows(double: DeviceDouble) -> list[str]:
    with mock.patch.dict(os.environ, double.environment()):
        api = net_route_test.UltimateApi("127.0.0.1", None, 5)
        return net_route_test.read_link_rows(api.machine)


def case_a_menu_that_will_not_open() -> None:
    with DeviceDouble() as double:
        double.faults.menu_screen_404 = True
        with mock.patch.object(net_route_test, "MENU_OPEN_SECONDS", 0.6):
            selftest.expect("no rows", read_rows(double), [])


def case_rows_read_over_rest() -> None:
    with DeviceDouble() as double:
        double.menu_rows = [*ROOT, NET_UP, WIFI_DOWN]
        reason = net_route_test.link_problem(read_rows(double), WIRED, [WIFI])
        selftest.expect("WiFi down read through the menu_screen decode",
                        "WiFi link is down" in reason, True)


def case_absent_interface_is_a_skip() -> None:
    with AbsentInterface() as double:
        double.configs[CATEGORY] = {SETTING: "Disabled"}
        code, output, seconds = run_suite(double)
        selftest.expect("exit code", code, 0)
        selftest.expect("reported as skipped", "SKIP" in output, True)
        selftest.expect("not reported as a failure", "FAIL" in output, False)
        selftest.expect("the idle failure is absent",
                        "did not return to Idle" in output, False)
        selftest.expect("the setting is put back", double.configs[CATEGORY][SETTING], "Disabled")
        writes = [r.params.get("value") for r in double.calls("PUT")
                  if r.path.endswith(f"/{SETTING.replace(' ', '%20')}")]
        selftest.expect("the setting was enabled, then restored", writes, ["Enabled", "Disabled"])
        control_writes = [r for r in double.calls("PUT", "/v1/machine:writemem")
                          + double.calls("POST", "/v1/machine:writemem")
                          if r.params.get("address", "").upper() == "DF1C"]
        selftest.expect("no write to the control register", len(control_writes), 0)
        if seconds >= RELEASE_WAIT_SECONDS:
            raise Failure(f"took {seconds:.1f}s, which is a wait for an interface that is absent")


def main() -> int:
    try:
        for case in (case_links_both_up, case_wifi_down_with_its_address_kept,
                     case_ethernet_down_with_its_address_kept,
                     case_wifi_not_connected_has_no_link_row, case_no_interface_rows,
                     case_a_menu_that_will_not_open, case_rows_read_over_rest):
            with check(case.__name__.removeprefix("case_").replace("_", " ")):
                case()
        with check("an absent command interface ends the suite as skipped, setting restored"):
            case_absent_interface_is_a_skip()
    except Failure as exc:
        suite_fail(SUITE, str(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
