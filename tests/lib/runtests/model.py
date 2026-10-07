from dataclasses import dataclass


@dataclass
class Result:
    category: str
    mode: str
    name: str
    verdict: str
    seconds: float
    # Set when the device could not be made healthy, so the remaining suites would
    # each fail for the same reason and tell nobody anything new. A device that
    # still answers but is permanently degraded counts: every suite after it
    # fails on the same dead listener.
    device_unhealthy: bool = False
    # How many times the device had to be recovered around this suite.
    recoveries: int = 0
    # How many times this suite ran, counting the first. Anything above one
    # is a suite that failed and was run again, whatever made it fail, and it
    # is what the retried exit status and the report's retry section read.
    attempts: int = 1
    # What a WARN verdict is about, when it is not the UI state.
    note: str = ""


@dataclass
class Options:
    host: str
    password: str
    timeout: str
    soak_profile: str
    output_dir: str
    stop_on_fail: bool
    health_check: bool
    # How many times a suite may run in total, counting the first.
    attempts: int
    recover_command: str
    recover_max_per_suite: int
    recover_max_total: int
    recover_timeout: float
    # Whether the suites spool the screens they read. Defaulted, because it is
    # the only option here a caller can reasonably leave alone.
    screens: bool = True
    # The KERNAL the Software IEC suites run under, and whether they enable the
    # Command Interface for it; see tests/lib/kernal.py.
    kernal: str = ""
    command_interface: bool = False
