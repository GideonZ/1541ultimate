from __future__ import annotations

import re
import subprocess
import time

from u64_connection_runtime import ProbeOutcome, RuntimeSettings, first_non_empty_line


def run_probe(settings: RuntimeSettings, correctness, *, context=None) -> ProbeOutcome:
    del correctness, context
    started_at = time.perf_counter_ns()
    try:
        result = subprocess.run(
            ["ping", "-n", "-c", "1", "-W", "2", settings.host],
            capture_output=True,
            text=True,
            timeout=4,
            check=False,
        )
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        if result.returncode == 0:
            match = re.search(r"time=([0-9.]+)", result.stdout)
            if match:
                return ProbeOutcome("OK", f"ping_reply_ms={match.group(1)}", elapsed_ms)
            return ProbeOutcome("OK", "ping reply", elapsed_ms)
        detail = first_non_empty_line(result.stderr + "\n" + result.stdout, "ping failed")
        return ProbeOutcome("FAIL", detail, elapsed_ms)
    except Exception as error:
        elapsed_ms = (time.perf_counter_ns() - started_at) / 1_000_000.0
        return ProbeOutcome("FAIL", f"ping failed: {error}", elapsed_ms)
