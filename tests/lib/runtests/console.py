import os
import re
import subprocess
import sys
import threading

import report


# Anything a terminal reads as formatting rather than as text: the colour
# escapes report.py emits, and the cursor and erase sequences a suite could
# emit. Stripped on the way to a file so a saved log greps cleanly, and left
# alone on the way to the console so the terminal keeps its colour.
ANSI_ESCAPES = re.compile(
    rb"\x1b\[[0-9;?]*[ -/]*[@-~]"        # CSI, which is what colour uses
    rb"|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)"  # OSC, terminated either way
    rb"|\x1b[@-Z\\-_]")                   # a two-character escape


def for_the_log(line: bytes) -> bytes:
    """One console line as it is saved: no escapes, and no device password.

    The console keeps its escapes so the terminal stays readable, and the file
    loses them so it greps cleanly. The password goes from both copies of the
    file because the artefacts leave the machine that produced them, and a
    suite that printed the password it was given would otherwise put it in one.
    """
    line = ANSI_ESCAPES.sub(b"", line)
    for secret in report.secrets():
        line = line.replace(secret.encode("utf-8", "replace"),
                            report.SECRET_MASK.encode())
    return line


class ConsoleCapture:
    """Copy this process's console output into a file as well as showing it.

    At the file-descriptor level rather than by replacing `sys.stdout`, because
    the UI-state gate and the operator's recovery command are subprocesses that
    inherit this process's stdout, and when a recovery fails its output is the
    only record of why. A Python-level tee would miss both.

    The console keeps its escapes and the file does not, so the terminal stays
    readable and the saved log greps cleanly.
    """

    def __init__(self, path: str) -> None:
        self.console_fd = os.dup(1)
        # Closed by stop(); the capture outlives this scope by design.
        self.handle = open(path, "wb")  # noqa: SIM115
        read_fd, write_fd = os.pipe()
        sys.stdout.flush()
        os.dup2(write_fd, 1)
        os.close(write_fd)
        self._read_fd = read_fd
        self._thread = threading.Thread(target=self._pump, name="run-log",
                                        daemon=True)
        self._thread.start()

    def _pump(self) -> None:
        pending = b""
        while True:
            try:
                chunk = os.read(self._read_fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            os.write(self.console_fd, chunk)
            pending += chunk
            *lines, pending = pending.split(b"\n")
            for line in lines:
                self.handle.write(for_the_log(line) + b"\n")
            self.handle.flush()
        if pending:
            self.handle.write(for_the_log(pending) + b"\n")
        self.handle.flush()

    def close(self) -> None:
        """Put the console back and let the pump drain what is left."""
        sys.stdout.flush()
        # Restoring fd 1 drops the last reference to the pipe's write end, so
        # the pump reads end-of-file and finishes what it has.
        os.dup2(self.console_fd, 1)
        self._thread.join(timeout=5)
        os.close(self._read_fd)
        self.handle.close()
        os.close(self.console_fd)


# The one console capture a process has, when it was asked for one.
_CONSOLE: ConsoleCapture | None = None


def start_console_capture(path: str) -> None:
    """Send this process's console output to `path` as well as to the console."""
    global _CONSOLE
    if _CONSOLE is None:
        _CONSOLE = ConsoleCapture(path)


def stop_console_capture() -> None:
    global _CONSOLE
    if _CONSOLE is not None:
        _CONSOLE.close()
        _CONSOLE = None


def console_fd() -> int:
    """The descriptor this run's console output started on.

    While a capture is running fd 1 is a pipe into the run log, so a suite's
    own lines go here instead: they belong in the suite's log rather than a
    second time in the runner's.
    """
    return _CONSOLE.console_fd if _CONSOLE else 1


def run_and_capture(command: list[str], environment: dict[str, str],
                    path: str, truncate: bool, console_fd: int) -> int:
    """Run a suite, sending every line to the console and to `path`.

    Three properties, each of which has to be held deliberately:

    - The file is appended to across attempts and truncated on the first,
      matching the per-suite JSONL rule, so a retried suite keeps every attempt.
    - Stderr is merged into the same file, in order. A traceback interleaved
      with the check line that produced it is the evidence, and two files would
      lose the interleaving.
    - The output is written as it arrives rather than at the end. A suite runs
      for minutes, and both destinations need its lines while it is happening.

    The console copy goes to the descriptor the run started with rather than
    through this process's stdout, so a suite's lines land in the suite's own
    log and not a second time in the runner's.
    """
    process = subprocess.Popen(command, env=environment,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    with open(path, "wb" if truncate else "ab") as handle:
        pending = b""
        while True:
            chunk = process.stdout.read1(65536)
            if not chunk:
                break
            os.write(console_fd, chunk)
            pending += chunk
            *lines, pending = pending.split(b"\n")
            for line in lines:
                handle.write(for_the_log(line) + b"\n")
            handle.flush()
        if pending:
            # A suite killed mid-line ends here, and the partial line is
            # itself the answer, so it is kept rather than dropped.
            handle.write(for_the_log(pending) + b"\n")
    process.stdout.close()
    return process.wait()
