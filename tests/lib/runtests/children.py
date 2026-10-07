import argparse
import os
import signal
import subprocess
import sys
from collections.abc import Sequence

import targets as targets_lib


# ---------------------------------------------------------------------------
# Several targets: several ordinary runs of this program, one per target.
#
# Nothing in this module knows that more than one device exists. A suite is
# started the same way, the device is driven the same way, and the report is
# written the same way; the only thing added here is which of them may be
# running at the same time, and whose line is whose.
# ---------------------------------------------------------------------------

# The options a child needs to run the same work its parent was asked for.
# Every option the parser accepts is either listed here or named in
# CHILD_EXCLUDED_OPTIONS below, and runner_policy_test.py fails when one is in
# neither: an option silently dropped here would make a multi-target run quietly
# different from the single-target run it is supposed to repeat.
CHILD_FORWARDED_FLAGS = ("e2e", "perf", "soak", "all", "manual", "syslog",
                         "record", "command_interface")
CHILD_FORWARDED_VALUES = (
    ("profile", "--profile"),
    ("modes", "--mode"),
    ("password", "--password"),
    ("timeout", "--timeout"),
    ("soak_profile", "--soak-profile"),
    ("kernal", "--kernal"),
    ("recover_command", "--recover-command"),
    ("recover_max_per_suite", "--recover-max-per-suite"),
    ("recover_max_total", "--recover-max-total"),
    ("recover_timeout", "--recover-timeout"),
    ("attempts", "--attempts"),
    ("color", "--color"),
    ("syslog_port", "--syslog-port"),
    ("record_menu_min_interval_ms", "--record-menu-min-interval-ms"),
    ("record_layout", "--record-layout"),
    ("record_quality", "--record-quality"),
    ("record_scale", "--record-scale"),
    ("record_fps", "--record-fps"),
    ("record_keyint", "--record-keyint"),
    ("record_ffmpeg_args", "--record-ffmpeg-args"),
)
CHILD_FORWARDED_NEGATIVE = (
    ("health_check", "--no-health-check"),
    ("restore_settings", "--no-restore-settings"),
    ("screens", "--no-screens"),
    ("record_video", "--no-record-video"),
    ("record_audio", "--no-record-audio"),
    ("record_menu", "--no-record-menu"),
    ("record_stamp", "--no-record-stamp"),
)
# host and targets become the child's one target; output_dir is rewritten per
# target so two children cannot write the same file; stop_on_fail is forwarded
# by hand because it is a short flag with a different destination name; list
# never reaches a child, because the parent prints the registry and stops;
# assume_fix and validate_openapi reach every child through the environment
# instead, because a suite started by hand has to be able to set them the same
# way (see machine.ASSUME_ENV and openapi_contract.ENV_FLAG).
# --list-profiles, --measured and --format answer a question and exit without
# starting a suite at all, so there is no child to forward them to.
CHILD_EXCLUDED_OPTIONS = ("host", "targets", "output_dir", "stop_on_fail", "list",
                          "assume_fix", "validate_openapi",
                          "list_profiles", "measured", "format")


def child_command(args: argparse.Namespace, target: targets_lib.Target,
                  output_dir: str) -> list[str]:
    """The command that runs `target` on its own, with this run's options."""
    command = [sys.executable, os.path.abspath(__file__)]
    for name in CHILD_FORWARDED_FLAGS:
        if getattr(args, name):
            command.append("--" + name.replace("_", "-"))
    for name, flag in CHILD_FORWARDED_VALUES:
        command += [flag, str(getattr(args, name))]
    for name, flag in CHILD_FORWARDED_NEGATIVE:
        if not getattr(args, name):
            command.append(flag)
    for suite in args.suite:
        command += ["--suite", suite]
    if args.stop_on_fail:
        command.append("--stop-on-fail")
    if output_dir:
        command += ["--output-dir", output_dir]
    command.append(target.token)
    return command


def schedulable(target: targets_lib.Target,
                active: Sequence[targets_lib.Target]) -> bool:
    """Whether `target` can start now: no machine it needs is in use."""
    return not any(target.conflicts_with(other) for other in active)


def signal_group(process: "subprocess.Popen", number: int) -> None:
    """Signal a child and the suite process it is running.

    The child is a run-tests of its own, and what actually drives the device is
    the suite it started. Signalling only the child would leave that suite
    running against hardware the operator now thinks is free, so the whole
    session started for the child is signalled instead.
    """
    try:
        os.killpg(os.getpgid(process.pid), number)
    except (ProcessLookupError, PermissionError):
        process.send_signal(number)


def stop(runs: "Sequence[ChildRun]", first: int) -> None:
    """End these children, escalating to a kill if they do not go."""
    runs = list(runs)
    for run in runs:
        signal_group(run.process, first)
    for run in runs:
        try:
            run.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            signal_group(run.process, signal.SIGKILL)
            run.process.wait()


class ChildRun:
    """One target's run: its process, and the half-line left in its pipe."""

    def __init__(self, target: targets_lib.Target, command: list[str],
                 slot: int) -> None:
        self.target = target
        self.slot = slot
        self.prefix = f"[{target.token}] "
        # Which of the concurrent runs this one is, so a suite that has to
        # listen on a port of its own can pick one no other run will pick.
        # The ftp-client suite runs a real FTP server for the device to
        # connect back to, and two of them bound the same control port: the
        # second died with "Address already in use" before its first check.
        # Slots are reused as runs finish, so this is the lowest free index
        # rather than the target's position in the list.
        environment = dict(os.environ, E2E_PORT_SLOT=str(slot))
        # Its own session, so signalling the child reaches the suite process
        # the child is running rather than only the child itself.
        self.process = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            start_new_session=True, env=environment)
        self.pending = b""
        self.drained = False

    def consume(self) -> bool:
        """Print whatever has arrived. Returns whether the pipe is still open.

        read1 rather than read: read would block until it had the whole 64K or
        the child exited, which is the opposite of streaming its output.
        """
        chunk = self.process.stdout.read1(65536)
        if not chunk:
            if self.pending:
                self.emit(self.pending)
                self.pending = b""
            self.drained = True
            return False
        self.pending += chunk
        *lines, self.pending = self.pending.split(b"\n")
        for line in lines:
            self.emit(line)
        return True

    def emit(self, line: bytes) -> None:
        # Prefixed per complete line, so a child's own colour and column
        # alignment survive and every line says which device it came from.
        print(self.prefix + line.decode("utf-8", "replace").rstrip("\r"), flush=True)
