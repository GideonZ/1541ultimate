from __future__ import annotations

import re
import subprocess
import time

from connection_runtime import ProbeOutcome, RuntimeSettings, first_non_empty_line


def run_probe(settings: RuntimeSettings, correctness, *, context=None) -> ProbeOutcome:
    del correctness, context
    started_at = time.perf_counter_ns()
    try:
        # Reachable is one reply within three seconds, with an echo sent every second
        # until then. A device whose receive queue is full under the stress profile drops
        # an echo now and then, as an Ultimate II+L does about one in fifty over Ethernet;
        # that is a lost datagram, not a device gone from the network.
        result = subprocess.run(
            ["ping", "-n", "-c", "1", "-w", "3", settings.host],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        if result.returncode == 0:
            sent = re.search(r"(\d+) packets transmitted", result.stdout)
            echoes = f" echoes={sent.group(1)}" if sent else ""
            match = re.search(r"time=([0-9.]+)", result.stdout)
            if match:
                return ProbeOutcome("OK", f"ping_reply_ms={match.group(1)}{echoes}", elapsed_ms)
            return ProbeOutcome("OK", f"ping reply{echoes}", elapsed_ms)
        summary = re.search(r".*packets transmitted.*", result.stdout)
        detail = summary.group(0) if summary else first_non_empty_line(result.stderr + "\n" + result.stdout, "ping failed")
        return ProbeOutcome("FAIL", detail, elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("FAIL", f"ping failed: {error}", elapsed_ms)
