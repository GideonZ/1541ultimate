import http.client
import os
import shlex
import signal
import subprocess
import time
from collections.abc import Callable, Sequence

import api as api_lib
import config_snapshot
import health
import pacing
import report
import targets as targets_lib
from api import UltimateApi
from report import Failure
from runtests.model import Options


# What a device call can raise past tests/lib/rest.py's own handler. rest.py
# catches OSError, TimeoutError and URLError; an http.client.HTTPException that
# is not an OSError, such as a truncated response from a device being reset
# mid-read, is not caught there, and a malformed body reaches a decoder as a
# TypeError or a ValueError. A capture taken from a device that has just been
# taken down meets all of these, and none of them may end the run.
DEVICE_ERRORS = (Failure, OSError, TimeoutError, ValueError, TypeError,
                 http.client.HTTPException)

# How long a device gets to come back before the run gives up on it, and how
# often to ask. Comfortably past the roughly 18 seconds the device needs to
# free an abandoned HTTP connection, so being busy is not mistaken for gone.
DEVICE_RECOVERY_BUDGET_SECONDS = 60.0

# How long the check after a suite's last attempt waits for the device. Long
# enough that a device which is merely busy is not called gone, because that
# answer abandons the remaining suites; short enough that a run with several
# failed suites does not spend a minute on each one deciding something the
# next suite's precondition decides anyway.
LAST_ATTEMPT_HEALTH_BUDGET_SECONDS = 10.0
DEVICE_RECOVERY_POLL_SECONDS = 2.0
# The recovery probe gets its own short per-request timeout rather than the
# run's --timeout. The shared client retries a GET three times, so at the
# 30 second default one probe could outlast the whole budget on a device that
# accepts connections but never answers, and the budget would mean nothing.
DEVICE_RECOVERY_PROBE_TIMEOUT_SECONDS = 5.0
# After the recovery command has run, the device is booting rather than busy,
# so it gets its own longer budget to come back in.
POST_RECOVERY_BUDGET_SECONDS = 180.0
DEFAULT_RECOVER_TIMEOUT_SECONDS = 900.0
# Two ceilings, because they answer different questions. The per-suite one
# stops a single suite that reliably takes the device down from consuming the
# whole run's budget on its own. The run-wide one stops a device that is failing
# for a reason no amount of recovering will fix.
DEFAULT_RECOVER_MAX_PER_SUITE = 3
DEFAULT_RECOVER_MAX_TOTAL = 10


def expand_recover_command(command: str, host: str, password: str,
                           timeout: float) -> str:
    """Fill in the target's own details, so one command serves every target.

    A run naming several targets runs one of these processes per target, and
    each is given the same `--recover-command` string. A recovery tool has to
    be told which device to work on, and there is nothing else to tell it
    with: the tool is started by the shell, not as a suite, so it sees none of
    the E2E_* variables a suite is given.

    The tokens are the ones a suite's arguments already use, so a recovery
    command reads like the rest of the registry:

        --recover-command 'tooling/recover_ultimate.py @HOST@'

    @PASS@ is quoted, because unlike a suite's argument list this string is
    handed to a shell.
    """
    command = command.replace("@HOST@", host)
    command = command.replace("@PASS@", shlex.quote(password))
    return command.replace("@TIMEOUT@", f"{timeout:g}")


class Device:
    """The few device calls the harness itself makes, outside any suite."""

    def __init__(self, host: str, password: str, timeout: float,
                 recover_command: str = "", health_check: bool = True,
                 recover_max_per_suite: int = 0, recover_max_total: int = 0,
                 recover_timeout: float = DEFAULT_RECOVER_TIMEOUT_SECONDS) -> None:
        self.health_check = health_check
        self.host = host
        self.password = password
        self.api = UltimateApi(host, password, timeout)
        self.probe = UltimateApi(host, password,
                                 DEVICE_RECOVERY_PROBE_TIMEOUT_SECONDS)
        self.recover_command = expand_recover_command(recover_command, host,
                                                      password, timeout)
        self.recover_max_per_suite = recover_max_per_suite if recover_command else 0
        self.recover_max_total = recover_max_total if recover_command else 0
        self.recover_timeout = recover_timeout
        self.recoveries = 0
        self.suite_recoveries = 0
        # What the device reported when the run first looked, so a reboot into
        # other firmware (a RAM-loaded image falls back to the flashed one) is
        # found instead of being tested.
        self.firmware: tuple[str, str, str] | None = None

    def firmware_identity(self) -> tuple[str, str, str] | None:
        """The product, version and commit the device reports, or None."""
        try:
            info = self.probe.info()
        except Failure:
            return None
        return (info.product, info.firmware_version,
                str(info.extra.get("git_commit_hash", "")))

    def firmware_problem(self) -> str:
        """Why this is not the firmware the run started on, or "" when it is."""
        now = self.firmware_identity()
        if now is None:
            return ""
        if self.firmware is None:
            self.firmware = now
            return ""
        if now == self.firmware:
            return ""
        return ("it runs different firmware: started on "
                f"{' '.join(self.firmware)}, now {' '.join(now)}")

    def start_suite(self) -> None:
        """Reset the per-suite recovery budget. Called once per suite run."""
        self.suite_recoveries = 0

    @property
    def just_reset(self) -> bool:
        """Whether the device was reset and nothing has changed it since.

        Reads do not count, so the health check between a reset and the next
        suite does not invalidate it. See api.MachineApi.reset.
        """
        return self.api.machine.was_just_reset

    def may_recover(self) -> tuple[bool, str]:
        """Whether another recovery is allowed, and why not when it is not."""
        if not self.recover_command:
            return False, "no --recover-command was given"
        if self.recoveries >= self.recover_max_total:
            return False, (f"the run has already used its {self.recover_max_total} "
                           "recoveries")
        if self.suite_recoveries >= self.recover_max_per_suite:
            return False, (f"this suite has already used its "
                           f"{self.recover_max_per_suite} recoveries")
        return True, ""

    def health_sweep(self) -> health.Health:
        """One health sweep, using this run's own short-timeout client."""
        return health.probe(self.host, self.password, api=self.probe)

    def release_input(self) -> bool:
        try:
            self.api.machine.release_all()
            return True
        except Failure:
            return False

    def reset(self) -> bool:
        try:
            self.api.machine.reset()
            return True
        except Failure:
            return False

    def wait_until_reachable(self, budget: float) -> bool:
        """Whether the device answers within `budget`, polling until it does.

        The UI-state gate cannot tell "the menu will not come clean" from "the
        device is no longer answering", and reported both as the former. One
        dead device then produced a precondition failure for every suite left
        in the run, each naming the UI, which is not where the problem was.

        The budget has to be generous, because most of the time the answer is
        that the device is busy rather than gone: it serves few HTTP
        connections at once and frees an abandoned one after about 18 seconds.
        A short probe turns that into a false verdict, and this one abandons
        the whole run, so being impatient here is worse than not checking at
        all. Observed live: a probe of three seconds called the device lost
        while it was answering again moments later.

        UltimateApi.reachable() is the probe, and it catches Failure only. A
        blanket except here would report a coding mistake in the probe itself
        as an unreachable device, and abandon the whole run for it.
        """
        deadline = time.monotonic() + budget
        while True:
            if self.probe.reachable():
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(DEVICE_RECOVERY_POLL_SECONDS)

    def run_recovery_command(self) -> bool:
        """Run the operator's recovery command and wait for the device.

        The command is the operator's, not the tree's: what brings a device
        back differs per setup (a JTAG download, a power switch, a flash tool),
        and none of that belongs in a test repository. It is run through the
        shell so the flag can carry arguments, and its output is left on this
        process's stdout, because when a recovery fails that output is the only
        record of why.

        Returns whether the device answered afterwards. A command that exits
        non-zero is still followed by the probe: some recovery tools report a
        failure the device has already recovered from.
        """
        report.step_start(f"recovery: {self.recover_command}")
        self.recoveries += 1
        self.suite_recoveries += 1
        # Its own process group, so a command that outlives the timeout is
        # stopped with everything it started: killing only the shell leaves the
        # tool running, holding this process's output open.
        try:
            process = subprocess.Popen(self.recover_command, shell=True,
                                       start_new_session=True)
        except OSError as exc:
            report.check_fail(f"could not be started: {exc}")
            return False
        try:
            returncode = process.wait(timeout=self.recover_timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            report.check_fail(f"no answer within {self.recover_timeout:g}s")
            return False
        if returncode != 0:
            report.check_warn(f"exited {returncode}; probing anyway")
        else:
            report.check_ok()
        report.step_start("recovery: device answers again")
        if self.wait_until_reachable(POST_RECOVERY_BUDGET_SECONDS):
            report.check_ok()
            return True
        report.check_fail(f"still unreachable after {POST_RECOVERY_BUDGET_SECONDS:g}s")
        return False

    def health_problem(self, label: str, patient: bool = True,
                  extra: Callable[[], str] | None = None,
                  budget: float | None = None, firmware: bool = True) -> str:
        """Why the device is not healthy enough to test on, or "" when it is.

        Health is the runner's one notion of a usable device, and everything
        else keys off it:

          it answers    `wait_until_reachable`, patiently by default, because
                        a busy device is far more common than a gone one.
          it is usable  `extra`, which for an E2E suite is the UI-state gate.
                        A device can answer every request and still be useless:
                        seen live, a browser stuck in a deleted directory with
                        the UI task deaf to injected keys, while REST, FTP,
                        telnet and the DMA port all answered normally. Nothing
                        else here notices that, which is why the gate is part
                        of health rather than a separate precondition.
          it is well    the health sweep: every listener the suites need, and a
                        C64 that is still running. Skipped under
                        --no-health-check, which leaves health meaning only
                        that the device answers and its UI can be driven. That
                        is the whole effect of the flag: a device with a
                        listener deliberately switched off would otherwise read
                        as unhealthy before every suite, forever.

        The order matters. The gate runs before the sweep because it shuts the
        menu, and the jiffy and raster checks are skipped while the menu is
        open, so running the sweep first would throw away its only reading of
        whether the C64 is alive.

        `budget` overrides the patient and impatient budgets for a caller that
        needs neither. The two are chosen for deciding whether to go on:
        patient waits out the recovery budget because a busy device is far
        more common than a gone one, and impatient asks once. A caller that is
        classifying a result rather than deciding whether to proceed wants
        something in between; see `run_suite`.

        Nothing here acts. Deciding and acting are separate so that the same
        answer can be used to log, to fail a precondition, and to recover.
        """
        if budget is None:
            budget = DEVICE_RECOVERY_BUDGET_SECONDS if patient else 0.0
        if not self.wait_until_reachable(budget):
            return "it is not answering"
        if extra is not None:
            problem = extra()
            if problem:
                return problem
        if not self.health_check:
            return ""
        sweep = self.sweep_health(label)
        if not sweep.ok:
            return "it is degraded: " + ", ".join(sweep.failed)
        return self.firmware_problem() if firmware else ""

    def ensure_healthy(self, label: str, patient: bool = True,
                 extra: Callable[[], str] | None = None) -> bool:
        """Establish that the device is healthy, recovering it once if it is not.

        One call runs at most one recovery, so a caller that wants several
        rounds loops and stays inside the ceilings on its own. Returns whether
        the device is healthy now.
        """
        problem = self.health_problem(label, patient, extra)
        if not problem:
            return True
        report.detail(f"{label} the device is unhealthy: {problem}")
        allowed, why = self.may_recover()
        if not allowed:
            report.detail(f"{label} not recovering: {why}")
            return False
        if not self.run_recovery_command():
            return False
        problem = self.health_problem(f"{label} after recovery,", patient, extra)
        if problem:
            report.detail(f"{label} still unhealthy after recovering: {problem}")
            return False
        return True

    def sweep_health(self, label: str) -> "health.Health":
        """Sweep the device, log one line, and record the whole sweep."""
        sweep = self.health_sweep()
        report.detail(f"{label} {sweep.one_line()}")
        for name in sweep.failed:
            note = sweep.detail_for(name)
            if note:
                report.detail(f"    {name}: {note}")
        report.health_result(
            label.rstrip(": ") or "device", sweep.ok,
            (health_entry(c) for c in sweep.checks))
        return sweep

    def close_active_menu(self) -> bool:
        """Back out of whatever the last suite left on the UI object stack.

        A menu-button toggle does not dismiss every nested UI object. In
        particular, AssemblySearch owns RUN/STOP itself. The shared
        implementation in tests/lib/api.py backs out through active editors,
        popups and browsers before using the toggle as a last resort.
        """
        try:
            self.api.machine.close_menu_from_anywhere()
            return True
        except Failure:
            return False


def capture_settings(target: targets_lib.Target, password: str
                     ) -> list[tuple[UltimateApi, config_snapshot.Snapshot]]:
    """Read every setting each machine this target occupies is running with.

    One entry per machine, so a cartridge target captures the cartridge and
    the computer it is plugged into: both are configured, and a suite can
    change either. A machine that will not answer is reported and left out,
    because a settings read that fails is not a reason not to run the suites.

    The client is given the short probe timeout rather than the run's, for the
    same reason the health probe is: this is the first thing that touches the
    device, and at the 30 second default a machine that accepts connections
    and never answers would cost three retries a read, three reads a store and
    twenty-odd stores before the first suite started.
    """
    captured: list[tuple[UltimateApi, config_snapshot.Snapshot]] = []
    # A computer's run also switches off the drives of the cartridge fitted in
    # it, so that machine's settings are put back too, or the next target that
    # runs on the cartridge starts with its drives off.
    fitted = [] if target.split else [
        c for c in targets_lib.declared_cartridges(target.device)
        if c not in target.resources]
    for host in [*target.resources, *fitted]:
        report.step_start(f"settings: capture {host}")
        api = UltimateApi(host, password, DEVICE_RECOVERY_PROBE_TIMEOUT_SECONDS)
        try:
            snapshot = config_snapshot.capture(host, api)
        except DEVICE_ERRORS as exc:
            report.check_warn(str(exc))
            report.detail(f"{host} keeps whatever settings this run leaves it")
            continue
        captured.append((api, snapshot))
        report.check_ok(f"{snapshot.item_count} settings in "
                        f"{len(snapshot.settings)} stores")
    return captured


def restore_settings(
        captured: Sequence[tuple[UltimateApi, config_snapshot.Snapshot]]) -> None:
    """Put back every setting the run changed, on each machine it captured.

    Warnings only. The run has produced its verdict by the time this runs, and
    a device that will not take a value back is a thing for the operator to
    see rather than a reason to change what the suites decided.

    This restores the running configuration, not the flash the machine boots
    from, and the two can end a run holding different values. A REST write
    only marks a store flash-stale; `ConfigBrowser::on_exit` in
    software/userinterface/config_menu.cc writes every stale store to flash
    when the config browser is left, and several suites open and leave it. So
    a value a suite set mid-run can reach flash, while the value put back here
    cannot: nothing opens the config browser after this point, so the restore
    is left stale and unwritten. The machine then reads the suite's value back
    on its next power cycle. Settings seen on this bench surviving a power
    cycle after a run that restored them are this, not a failed restore.
    """
    for api, snapshot in captured:
        report.step_start(f"settings: restore {snapshot.machine}")
        try:
            restored, refused = snapshot.restore(api)
        except DEVICE_ERRORS as exc:
            report.check_warn(str(exc))
            continue
        if refused:
            report.check_warn(f"{len(refused)} of "
                              f"{len(restored) + len(refused)} not put back")
        else:
            report.check_ok("unchanged" if not restored
                            else f"{len(restored)} put back")
        for change in restored:
            report.detail(str(change))
        for change, reason in refused:
            report.warn(f"settings: {change}: {reason}")


def health_entry(check: "health.Check") -> dict[str, object]:
    """One health check as a record, with its figures when it has any.

    The figures are present only on a check that measured any, so a sweep of
    latencies carries the four keys it always carries. They go here rather
    than into `detail`, which is a
    human sentence printed under a failing check's name, because packing three
    numbers into prose would make every consumer parse it back out.

    Under one name for every check that has any, rather than under the check's
    own name: two checks carry figures now, the heap's three and the ident's
    two syslog counters, and a key per check would make a consumer know the set
    of checks before it could read one.
    """
    entry: dict[str, object] = {"name": check.name, "state": check.state,
                                "ms": round(check.ms, 1), "detail": check.detail}
    if check.figures:
        entry["figures"] = dict(check.figures)
    return entry


def keep_iec_bus_to_target(target, options: Options, after: str) -> None:
    """Switch the other machine's drives off again when a suite brought them back.

    The run switches them off once, in RAM. A suite that powers the machine off,
    such as wake-on-wifi, also restarts a fitted cartridge, which then answers on
    the same IEC bus IDs from its stored settings until it is switched off again.
    """
    ensure = (api_lib.ensure_host_drives_off if target.split
              else api_lib.ensure_cartridge_drives_off)
    try:
        silenced = ensure(target, options.password, float(options.timeout))
    except DEVICE_ERRORS as exc:
        report.warn(f"after {after}: the other machine's drives could not be checked: {exc}")
        return
    if silenced:
        report.warn(f"after {after}: drives were back on the IEC bus and are off again "
                    f"({silenced})")


def reset_to_clean_slate(device: Device, phase: str) -> bool:
    for label, action in (
        (f"{phase}: release input", device.release_input),
        (f"{phase}: close active menu UI", device.close_active_menu),
        (f"{phase}: reset machine", device.reset),
    ):
        report.step_start(label)
        if not action():
            report.check_fail()
            return False
        report.check_ok()
    time.sleep(pacing.RESET_SETTLE_SECONDS)
    return True
