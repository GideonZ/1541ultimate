#!/usr/bin/env python3
"""Every Software IEC log line reaches the device log whole, whatever else is printing.

Other firmware tasks print to the same log a character at a time, and a line of theirs
printed into a Software IEC line splits it where the syslog breaks lines: the soak's log
correlation then has fragments to account for, and a reader of a debug log a line that
starts in the middle of another. This suite makes that happen as often as it can: with
"Log Every Operation" on, the C64 sends a few hundred commands, each of which writes a line,
while a REST client keeps the HTTP task printing its own lines. Then every line that
carries this run's marker has to be a whole Software IEC line, starting its syslog line and
ending in its sequence number.

Lines lost on the way (UDP) are reported and tolerated up to a tenth, and so is a line
that arrives twice with the same text: measured on an Ultimate II+L over WiFi, one line in
three hundred came twice, although the firmware sends each datagram once, as the soak's
log correlation also allows. A line that arrives in pieces, or one number with two texts,
is a failure.

Needs Software IEC on device 11 and the device's "Log to Syslog Server" set to this host. Under
run-tests --syslog it reads the log the runner collects, so every target of a run is covered;
otherwise it binds the device's syslog port itself, and a second target run at the same time
skips.
"""
import argparse
import re
import sys
import threading
import time
import traceback
import uuid
from pathlib import Path

sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                          if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
sys.path.insert(0, bootstrap.directory("soak", "io", "iec"))
import cli  # noqa: E402
import softiec_log  # noqa: E402
from api import UltimateApi  # noqa: E402
from config_snapshot import Snapshot  # noqa: E402
from iec_agent import OPEN, Agent  # noqa: E402
from report import Failure, check, check_skip, check_start, detail, suite_fail, suite_ok, teardown_step  # noqa: E402

SUITE = "log_lines_test"
CATEGORY = "SoftIEC Drive Settings"
COMMANDS = 300
# The REST client's pace: each request prints two lines, and the syslog task sends about
# 200 lines a second, so this keeps it busy without overflowing its buffer.
REQUEST_INTERVAL_S = 0.02
# The end of a Software IEC line: its answer and sequence number.
LINE_TAIL = re.compile(r" -> \d\d,.*,\d\d,\d\d #\d+$")


def log_source(api):
    """Where this device's log is read from, or None and why not: the runner's collected log
    when run-tests --syslog collects it, otherwise a UDP socket on the port it sends to."""
    collected = softiec_log.CollectedLogSource.from_environment(api.host)
    if collected is not None:
        collected.mark()
        return collected, f"the run's collected log {collected.path}"
    if "Network Settings" not in api.configs.category_names():
        return None, "this device has no Network Settings"
    parsed = softiec_log.parse_syslog_server(str(api.configs.get("Network Settings", "Log to Syslog Server")))
    if parsed is None:
        return None, "the device's 'Log to Syslog Server' setting is empty"
    ip, port = parsed
    if ip not in softiec_log.local_addresses(api.host):
        return None, f"the device logs to {ip}, which is not this host"
    source = softiec_log.UdpLogSource("", port)
    try:
        source.start()
    except OSError as exc:
        # Another process holds the port, such as this suite for a second target.
        return None, f"UDP {port} is taken ({exc}); run with run-tests --syslog to read the collected log"
    return source, f"UDP {port}"


def run(args):
    api = UltimateApi(args.host, args.password, args.timeout)
    source, note = log_source(api)
    if source is None:
        check_start("every Software IEC line reaches the log whole")
        check_skip(note)
        return
    detail(f"device log from {note}")
    agent = Agent(api)
    saved = Snapshot(args.host, {CATEGORY: api.configs.category(CATEGORY)})
    nonce = uuid.uuid4().hex[:8].upper()
    stop = threading.Event()
    requests = [0]

    def rest_client():
        while not stop.is_set():
            try:
                api.rest.json("/v1/version")
                requests[0] += 1
            except Exception:  # noqa: BLE001
                pass
            time.sleep(REQUEST_INTERVAL_S)

    started = False
    try:
        api.configs.set(CATEGORY, "Soft Drive Bus ID", 11)
        api.configs.set(CATEGORY, "IEC Drive", "Enabled")
        api.configs.set(CATEGORY, "Log Every Operation", "Enabled")
        agent.start()
        started = True
        agent.call(OPEN, channel=15)
        agent.status(tuple(range(100)))
        client = threading.Thread(target=rest_client, daemon=True)
        client.start()
        for i in range(COMMANDS):
            # The soak's marker command: it answers 30 and so always writes a line.
            agent.command(softiec_log.marker_text(f"{nonce}{i:04d}"), allowed=(30,))
        stop.set()
        client.join(timeout=5)
        time.sleep(4)  # the syslog task sends at a limited rate
        detail(f"{COMMANDS} commands, {requests[0]} REST requests alongside")
        if requests[0] < COMMANDS:
            raise Failure(f"only {requests[0]} REST requests were answered alongside the commands")

        with check("every Software IEC line reaches the log whole"):
            entries = source.entries()
            marked = [(ip, text) for ip, text in entries if nonce in text]
            texts = [text for _ip, text in marked]
            fragments = [t for t in texts
                         if not t.startswith(softiec_log.LOG_PREFIX)
                         or softiec_log.parse_line(t) is None]
            # A line split inside the nonce leaves it whole in neither piece.
            fragments += [t for _ip, t in entries if nonce not in t and (
                (t.startswith(softiec_log.LOG_PREFIX) and softiec_log.parse_line(t) is None)
                or (not t.startswith(softiec_log.LOG_PREFIX) and LINE_TAIL.search(t)))]
            detail(f"{len(texts)} lines carry the marker, {len(fragments)} of them in pieces")
            for text in fragments[:5]:
                detail(f"  in pieces: {text[:160]!r}")
            if fragments:
                raise Failure(f"{len(fragments)} Software IEC lines arrived in pieces")
            seen = {}
            for ip, text in marked:
                seen.setdefault(softiec_log.parse_line(text).seq, []).append((ip, text))
            twice = {n: copies for n, copies in seen.items() if len(copies) > 1}
            differ = {n: copies for n, copies in twice.items() if len({t for _ip, t in copies}) > 1}
            detail(f"{len(twice)} lines delivered twice")
            for n, copies in list(differ.items())[:3]:
                detail(f"  #{n} arrived with different texts: {copies!r}"[:400])
            if differ:
                raise Failure(f"{len(differ)} sequence numbers arrived with different texts")
            whole = len(seen)
            if whole < COMMANDS * 9 // 10:
                raise Failure(f"only {whole} of {COMMANDS} lines arrived, too few to judge")
            detail(f"{COMMANDS - whole} lines lost on the way")
    finally:
        stop.set()
        source.stop()
        teardown_step("restore the drive settings", lambda: saved.restore(api))
        if started:
            teardown_step("return the C64 to BASIC", lambda: api.machine.reset(force=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cli.add_device_arguments(parser)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:
        traceback.print_exc()
        suite_fail(SUITE, str(exc))
        return 1
    suite_ok(SUITE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
