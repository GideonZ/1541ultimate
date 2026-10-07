import argparse
import json
import os
import socket
import time
from collections.abc import Sequence

import report
import syslog_collector
import targets as targets_lib
from api import UltimateApi
from report import Failure
from runtests.device import DEVICE_ERRORS, Device


# Set by the process that binds the device log port, so a child it starts
# knows not to bind it as well.
SYSLOG_OWNED_ENV = "E2E_SYSLOG_OWNED"

# What the firmware sends to when the configured value carries no `:<port>`.
# `Syslog::init` in software/network/syslog.cc.
SYSLOG_DEVICE_DEFAULT_PORT = 514

# The port the collector actually bound, exported to every suite the run
# starts. The device-free gate is what uses it: a stub suite sends a datagram
# into the running collector to prove the file it writes is the file the `log`
# record names. The number cannot be chosen in advance, because two runs
# starting at the same moment are handed the same one, which is what a
# multi-target gate does by design. The collector binds first and says which
# port it got.
SYSLOG_PORT_ENV = "E2E_SYSLOG_PORT"

# Every port the collector bound, comma separated. A bench that gives each
# machine a port of its own has more than one, and a device pointed at a port
# nothing is listening on is the failure `syslog_setting_problem` exists to
# name, so the check needs the whole set rather than the default.
SYSLOG_PORTS_ENV = "E2E_SYSLOG_PORTS"

# Each target's collected log file, as JSON from target token to path. A suite that reads
# device log lines is given its own target's path as SYSLOG_FILE_ENV, because the collector
# holds the port and a second socket on it would take an arbitrary share of the lines.
SYSLOG_FILES_ENV = "E2E_SYSLOG_FILES"
SYSLOG_FILE_ENV = "E2E_SYSLOG_FILE"

# How long the parent waits for a device to say which port it logs to. This
# runs before the first suite and a device that does not answer costs the run
# nothing but attribution by source address, so it is short.
SYSLOG_SETTING_TIMEOUT_SECONDS = 5.0


def syslog_ports(machines: Sequence[str], password: str) -> dict[str, int]:
    """Which port each machine's own configuration says it logs to.

    Read, never written. `Syslog::init` runs once from `ultimate_main`, so the
    value is boot-time state and writing it during a run does nothing until
    the device reboots; a suite that changed it may have been testing exactly
    that, and putting it back would delete that suite's finding. See OBS-7.3.

    A port exactly one machine sends to is what lets the collector attribute a
    datagram by the socket that received it rather than by its source address,
    which is the only thing that works for a device logging from an interface
    the run did not expect. A machine that does not answer, or names no port,
    is left to the run's default port and to source-address attribution, which
    is what every machine had before.
    """
    found: dict[str, int] = {}
    for machine in machines:
        try:
            api = UltimateApi(machine, password,
                              SYSLOG_SETTING_TIMEOUT_SECONDS)
            value = str(api.configs.get(syslog_collector.CONFIG_STORE,
                                        syslog_collector.CONFIG_ITEM) or "")
        except (Failure, OSError, TimeoutError, ValueError) as exc:
            report.detail(f"device log:  {machine} did not say which port it "
                          f"logs to ({report.format_exception(exc)}), so its "
                          f"lines are attributed by source address")
            continue
        _host, _, port = value.rpartition(":")
        if port.isdigit() and 0 < int(port) < 65536:
            found[machine] = int(port)
    return found


def start_syslog(args: argparse.Namespace,
                 wanted: Sequence[targets_lib.Target],
                 directory: str = "") -> "syslog_collector.Collector | None":
    """Open the device log collector for this run, or say why it did not open.

    Started in the process that owns the whole run, because it binds one port
    and maps source addresses to targets: it has to know every target and there
    has to be exactly one of it. A collector that cannot start reports the
    reason here, before any suite runs, rather than late in a run that has
    already cost 15 to 30 minutes.
    """
    if not (args.syslog and args.output_dir):
        return None
    if os.environ.get(SYSLOG_OWNED_ENV):
        # A child of a multi-target run. Exactly one process binds the port:
        # two sockets on one unicast UDP port each get about half the
        # datagrams and neither looks wrong. The child still checks its own
        # device's setting, which is the device it owns.
        return None
    # The run's root, not one target's directory: the collector writes one
    # file per machine under each target's own slug, so it is handed the tree
    # it composes those paths against.
    root = directory or args.output_dir
    machines = sorted({machine for target in wanted
                       for machine in target.log_hosts})
    menu_addresses = {}
    for target in wanted:
        if target.device in menu_addresses:
            continue
        if target.device.startswith("127."):
            menu_addresses[target.device] = []
            continue
        try:
            found = syslog_collector.menu_addresses(
                target, args.password, float(args.timeout))
        except DEVICE_ERRORS as exc:
            report.warn(f"device log: {target.device} menu address discovery failed: "
                        f"{report.format_exception(exc)}")
            continue
        except RuntimeError as exc:
            report.warn(f"device log: {target.device} menu address discovery failed: {exc}")
            continue
        menu_addresses[target.device] = sorted(found)
        report.detail(f"device log:  {target.device} menu addresses: "
                      f"{', '.join(sorted(found)) or 'none'}")
    collector = syslog_collector.Collector(directory=root,
                                           port=args.syslog_port)
    opened = collector.bind(wanted, syslog_ports(machines, args.password),
                            menu_addresses)
    for problem in collector.problems:
        report.warn(f"device log: {problem}")
    if not opened:
        return None
    report.detail(f"device log:  collecting on UDP "
                  f"{', '.join(str(port) for port in collector.ports())}")
    for token, entries in sorted(collector.elsewhere.items()):
        for machine, where in entries:
            report.detail(f"device log:  {token}'s {machine} logs to {where}, "
                          f"which is that machine's own file")
    # In this process's own environment, so every suite it starts inherits it
    # through the copy run_one_attempt makes.
    os.environ[SYSLOG_PORT_ENV] = str(collector.port)
    os.environ[SYSLOG_PORTS_ENV] = ",".join(str(port)
                                            for port in collector.ports())
    # The device under test's own file; a cartridge's computer logs to a second one.
    os.environ[SYSLOG_FILES_ENV] = json.dumps(
        {token: path for token, path in collector.files()
         if os.path.basename(path) == "syslog.txt"})
    for token, path in collector.files():
        report.log_result(token, os.path.relpath(path, root),
                          collector.started, collector.port,
                          addresses=collector.addresses_of(token),
                          ports=collector.ports_of(token))
    check_syslog_reachable(collector, machines, args.password)
    return collector


# How long the collector waits to hear a machine it has just provoked. Two
# datagrams come back from a single `GET /v1/version` - the accept and the
# request - and they cross a LAN, so this is generous rather than tight.
SYSLOG_READY_SECONDS = 3.0


def check_syslog_reachable(collector, machines: Sequence[str],
                           password: str) -> list[str]:
    """Say at startup which machines the collector cannot hear. Never raises.

    A device whose log does not reach the collector produces an empty file and
    nothing else, and that is indistinguishable from a device that had nothing
    to say. Until this ran, the difference was only visible when the run ended,
    which is 15 to 30 minutes after the point where it could have been fixed,
    and OBS-1.2 is the requirement that a component says at startup what it
    cannot do.

    One `GET /v1/version` per machine is enough to provoke a line: measured on
    a C64 Ultimate and an Ultimate II+L, that one request produces two, the
    accepted connection and the request itself. So a machine that says nothing
    within a few seconds of being asked something is a machine whose log is
    not arriving, and the run says so while somebody can still act on it.

    It names no cause. A setting pointing elsewhere, a forwarding task that
    terminated at boot and a link dropping datagrams all produce this, and the
    facts that separate them are elsewhere: the setting the run read, the two
    counters on each ident sweep, and whether anything arrived from an address
    no target claims, which this reports beside it because it is the one of
    the three the collector itself knows.
    """
    silent: list[str] = []
    for machine in machines:
        try:
            UltimateApi(machine, password,
                        SYSLOG_SETTING_TIMEOUT_SECONDS).rest.request(
                            "GET", "/v1/version")
        except (Failure, OSError, TimeoutError, ValueError):
            # A machine that does not answer REST is a problem the health
            # sweep reports properly, and not this one's to duplicate.
            continue
    deadline = time.monotonic() + SYSLOG_READY_SECONDS
    while time.monotonic() < deadline:
        silent = [machine for machine in machines
                  if not collector.heard(machine)]
        if not silent:
            break
        time.sleep(0.1)
    for machine in silent:
        stranger = sum(collector.unknown_senders().values())
        report.warn(f"device log: {machine} sent nothing when this run asked "
                    f"it for /v1/version, so its log is not reaching the "
                    f"collector on UDP "
                    f"{collector.machine_ports.get(machine, collector.port)}"
                    + (f"; {stranger} line(s) did arrive from an address no "
                       f"target claims, which are in "
                       f"{syslog_collector.UNKNOWN_SENDER_NAME}"
                       if stranger else ""))
    return silent


def finish_syslog(collector) -> None:
    """Stop the collector and record what it saw, including what it missed.

    Three things a reader needs and only this knows: how much arrived, every
    interval a device that had been logging went quiet, and any problem the
    collector hit after it started. The last of those used to be reported only
    at startup, so a file that became unwritable mid-run discarded the rest of
    that device's log and said so nowhere.
    """
    already = len(collector.problems)
    collector.stop()
    gaps = collector.gaps()
    for gap in gaps:
        report.gap_result("syslog", gap["started"], gap.get("ended"),
                          target=gap["target"], machine=gap["machine"],
                          reason=f"no line from {gap['machine']}")
    root = collector.directory
    unknown = collector.unknown_senders()
    for token, _path in collector.files():
        if collector.observed(token):
            continue
        # Nothing at all from a device the run asked for a log from. Said
        # once, here, because an empty file is otherwise indistinguishable
        # from a device that had nothing to say.
        #
        # What is deliberately not said is why. Several causes produce this
        # one shape - a setting that names somewhere else, a setting written
        # during a run and not yet in force because `Syslog::init` reads it
        # only at boot, a forwarding task that terminated at `socket()` or
        # `connect()`, a link that dropped every datagram - and the run cannot
        # tell them apart from here. `Syslog::init` and `forwardLogging` both
        # print their failure, and both print it into the buffer that is not
        # being forwarded, so the account of the failure is inside the thing
        # that failed and no amount of collecting recovers it. A guess printed
        # in a fixed format is read as a finding, so the warning states what
        # was observed and names the facts that discriminate.
        report.warn(f"device log: {token} sent no line at all during this run, "
                    f"so its log is empty; the collector received "
                    f"{collector.lines} line(s) in total on UDP "
                    f"{', '.join(str(port) for port in collector.ports())}, "
                    f"and this run expected its lines from "
                    f"{', '.join(collector.addresses_of(token)) or 'no address'}"
                    f". The setting this run read at both ends, and whether "
                    f"anything reached {syslog_collector.UNKNOWN_SENDER_NAME}, "
                    f"are the facts that tell one silence from another; "
                    f"nothing here says whether the device sent lines that "
                    f"never arrived")
    for token, path in collector.files():
        # A second record for the same target, carrying what the first could
        # not know: which addresses the lines actually came from, and whether
        # the port or the address is what attributed them. The report reads
        # the two as one.
        report.log_result(token, os.path.relpath(path, root),
                          collector.started, collector.port,
                          addresses=collector.addresses_of(token),
                          ports=collector.ports_of(token),
                          senders=collector.observed(token),
                          attributed=collector.attribution_of(token),
                          unknown_senders=unknown)
    report.detail(f"device log:  {collector.lines} line(s) collected"
                  + (f", {collector.unmapped} from an unrecognised sender"
                     if collector.unmapped else "")
                  + (f", {len(gaps)} silence(s)" if gaps else ""))
    for problem in collector.problems[already:]:
        report.warn(f"device log: {problem}")


def check_syslog_setting(device: "Device", when: str) -> str:
    """What this device says it is sending its log to, and a warning when it is
    not sending it anywhere.

    Read rather than written, and never corrected. `Syslog::init` runs once
    from `ultimate_main`, so writing this during a run does nothing until the
    device reboots, and a suite that changed it may have been testing exactly
    that: putting it back would delete that suite's finding.

    The start read catches a device that was reflashed and lost the setting,
    which otherwise produces no log and no reason for it. The end read catches
    the one that produces no log in the *next* run, which is far harder to
    trace back, because a partial .cfg load can write stores it never names and
    Network Settings is one of them.
    """
    try:
        value = str(device.api.configs.get(syslog_collector.CONFIG_STORE,
                                           syslog_collector.CONFIG_ITEM) or "")
    except Failure as exc:
        report.warn(f"device log: the syslog setting could not be read {when}: "
                    f"{report.format_exception(exc)}")
        return ""
    if not value:
        report.warn(f"device log: this device is not configured to send its log "
                    f"anywhere ({when})")
        return value
    problem = syslog_setting_problem(value, device.host)
    if problem:
        report.warn(f"device log: {problem} ({when})")
    return value


def collector_address_for(machine: str) -> str:
    """Which of this host's addresses a datagram from `machine` would reach.

    A UDP socket that is connected and never written to: `connect` on a
    datagram socket only fixes the peer and picks a route, so nothing goes on
    the wire and `getsockname` then reports the local address that route
    chose. That is exactly the address the device has to be pointed at, and it
    is right on a host with several interfaces, where the host's name resolves
    to whichever one the resolver prefers rather than to the one that faces
    the bench.
    """
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((machine, syslog_collector.DEFAULT_PORT))
        return probe.getsockname()[0]
    except OSError:
        return ""
    finally:
        probe.close()


def syslog_setting_problem(value: str, machine: str = "") -> str:
    """Why this device's log will not reach this run's collector, or "".

    Two ways a setting that looks right sends the log nowhere, and both
    produce the same artefact: an empty file, no warning, and a report saying
    the device said nothing, which is the shape a device that had stopped
    also has.

    The port. A device pointed at `<host>` with no port sends to 514, because
    that is what `Syslog::init` defaults to, and the collector binds a
    non-privileged port because 514 needs root. Measured on the C64 Ultimate
    here: 23 suites, 0 lines, nothing anywhere saying why. The collector now
    binds every port the devices name, so what is left to catch is a port it
    could not open.

    The host. The value can name a machine that is not the one collecting, and
    a bench that has been reconfigured, or a runner host whose address has
    moved, produces exactly that. The comparison is against the address a
    datagram from this device would arrive at rather than against this host's
    name, because a host with several interfaces has several answers and only
    one of them is the one the device can reach.

    The host is compared as text and never resolved. The setting is what an
    operator typed, and a name that resolves here and not on the device is the
    mistake worth naming rather than one to be clever about, so a value that
    is not a literal address is reported as one this cannot check rather than
    as one that is wrong.
    """
    wanted_ports = [port for port in
                    (os.environ.get(SYSLOG_PORTS_ENV)
                     or os.environ.get(SYSLOG_PORT_ENV) or "").split(",")
                    if port]
    if not wanted_ports:
        return ""
    host, _, port = value.rpartition(":")
    if not host:
        # No colon at all, so the whole value is the host and the device will
        # use the firmware's default port.
        host, port = value, str(SYSLOG_DEVICE_DEFAULT_PORT)
    if port not in wanted_ports:
        return (f"this device sends its log to port {port} and this run "
                f"collects on {', '.join(wanted_ports)}, so none of it will "
                f"arrive; set '{syslog_collector.CONFIG_ITEM}' to "
                f"'{host}:{wanted_ports[0]}' and reboot the device")
    here = collector_address_for(machine) if machine else ""
    if here and host != here and not host.replace(".", "").isdigit():
        return (f"this device sends its log to '{host}', which is a name "
                f"rather than an address, so what it resolves to on the "
                f"device decides whether it reaches this run's collector at "
                f"{here}")
    if here and host != here:
        return (f"this device sends its log to {host} and this run collects "
                f"at {here}, so none of it will arrive; set "
                f"'{syslog_collector.CONFIG_ITEM}' to '{here}:{port}' and "
                f"reboot the device")
    return ""
