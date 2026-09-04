#!/usr/bin/env python3
# E2E: Verifies UCI transport state machine, command completion, and reply framing.

"""End-to-end check of the Ultimate Command Interface over the cartridge registers.

Regression guard for GideonZ/1541ultimate#740: CTRL_CMD_LOAD_REU ($08) and
CTRL_CMD_SAVE_REU ($09) never returned, so the command interface stayed BUSY for
every target until the machine was power-cycled.

It also covers the transport state machine, the control target's rejection paths,
and reply framing on the SoftIEC target, whose single-part replies were announced
as "Data More" and left a client waiting for a block that is never sent. On
Ultimate 64 hardware it verifies the runtime RGB palette commands and restores
the palette before exiting.

Every expected value here was taken from the firmware and confirmed against a
real device. The manuals under doc/ ("Ultimate Command Interface - Register API",
and the per-target summaries) describe the intended design and are useful
background, but they have drifted from the implementation in places, so they are
not used as the source of truth. Where the two differ, this suite follows the
firmware and says so at the assertion.

The registers live at $DF1B-$DF1F and are reached through REST
machine:readmem / machine:writemem, which perform DMA cycles on the cartridge
bus. No 6502 code is involved.

  $DF1C  write: control (bit 0 PUSH_CMD, bit 1 DATA_ACC, bit 2 ABORT, bit 3 CLR_ERR)
         read:  status  (bit 0 CMD_BUSY, bit 1 DATA_ACC, bit 2 ABORT_P, bit 3 ERROR,
                         bits 4-5 state, bit 6 STAT_AV, bit 7 DATA_AV)
  $DF1D  write: command byte queue;  read: identification ($C9, or $49 on IRQ)
  $DF1E  read:  response data queue
  $DF1F  read:  status data queue

Command bytes go into $DF1D one REST call at a time on purpose: machine:writemem
writes an ascending span, so a single multi-byte write would land on $DF1E onwards.
Only $DF1C is polled for the same reason -- a span read starting there would pop
the response and status queues.

Supported on any Ultimate whose FPGA provides the command interface. The suite
enables the "Command Interface" setting, changes REU settings, and restores all of
them on exit.
"""

import argparse
import ftplib
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

# The one stanza that puts the shared library on sys.path; see tests/lib/bootstrap.py.
sys.path.insert(0, str(next(p for p in Path(__file__).resolve().parents
                            if (p / "tests" / "lib").is_dir()) / "tests" / "lib"))
import bootstrap  # noqa: E402,F401
import cli  # noqa: E402
import ftp as ftp_lib
import rest as rest_lib
import targets
from report import (
    FAIL, Failure, OK, SKIP, check, check_skip, check_start, detail,
    format_exception, section, suite_fail, suite_ok, warn)


READMEM_PATH = "/v1/machine:readmem"
WRITEMEM_PATH = "/v1/machine:writemem"
RESET_PATH = "/v1/machine:reset"

CONFIG_CATEGORY = "C64 and Cartridge Settings"
CFG_CMD_IF = "Command Interface"
CFG_REU_ENABLE = "RAM Expansion Unit"
CFG_REU_IMAGE = "REU Preload Image"
CFG_REU_SIZE = "REU Size"
CFG_REU_OFFSET = "REU Preload Offset"
# Everything the suite writes, captured before the first change and put back at the end.
OWNED_SETTINGS = (CFG_CMD_IF, CFG_REU_ENABLE, CFG_REU_IMAGE, CFG_REU_SIZE, CFG_REU_OFFSET)

REG_CONTROL = 0xDF1C
REG_COMMAND = 0xDF1D
REG_RESPONSE = 0xDF1E
REG_STATUS = 0xDF1F

CTRL_PUSH_CMD = 0x01
CTRL_DATA_ACC = 0x02
CTRL_ABORT = 0x04
CTRL_CLR_ERR = 0x08

ST_ERROR = 0x08
ST_STATE_MASK = 0x30
ST_STATE_IDLE = 0x00
ST_STATE_BUSY = 0x10
ST_STATE_DATA_LAST = 0x20
ST_STATE_DATA_MORE = 0x30
ST_STAT_AV = 0x40
ST_DATA_AV = 0x80
STATE_NAMES = {ST_STATE_IDLE: "Idle", ST_STATE_BUSY: "Command Busy",
               ST_STATE_DATA_LAST: "Data Last", ST_STATE_DATA_MORE: "Data More"}
STATUS_FLAGS = ((0x01, "CMD_BUSY"), (0x02, "DATA_ACC"), (0x04, "ABORT_P"), (ST_ERROR, "ERROR"),
                (ST_STAT_AV, "STAT_AV"), (ST_DATA_AV, "DATA_AV"))

# Dispatch layer: the low nibble of the first command byte selects the target,
# bit 7 asks for a command that sends no reply at all.
TARGET_NO_REPLY = 0x80
TARGET_CONTROL = 0x04
TARGET_SOFTIEC = 0x05
# 1 and 2 are Ultimate DOS, 3 network, 4 control, 5 SoftIEC, 6 HTTP. $0F is the
# highest target the dispatcher can address and nothing registers there.
TARGET_UNREGISTERED = 0x0F

CTRL_CMD_IDENTIFY = 0x01
CTRL_CMD_LOAD_REU = 0x08
CTRL_CMD_SAVE_REU = 0x09
CTRL_CMD_GET_HWINFO = 0x28
CTRL_CMD_GET_PALETTE = 0x51
CTRL_CMD_SET_PALETTE = 0x52
CTRL_CMD_SET_PALETTE_COLOR = 0x53
CTRL_CMD_RESET_PALETTE = 0x54
SOFTIEC_CMD_IDENTIFY = 0x01
SOFTIEC_CMD_LOAD_SU = 0x10
SOFTIEC_CMD_GET_FATNAME = 0x22
# No target implements $7F, so it reaches the unknown-command path.
CMD_UNIMPLEMENTED = 0x7F

STATUS_OK = b"00,OK"
STATUS_UNKNOWN_COMMAND = b"21,UNKNOWN COMMAND"
STATUS_INVALID_PARAMS = b"81,INVALID PARAMS"
STATUS_REU_DISABLED = b"84,REU NOT ENABLED"
STATUS_REU_CANNOT_OPEN = b"85,REU FILE CANNOT BE OPENED"
STATUS_REU_NOT_SAVED = b"86,REU OFFSET > SIZE. NOT SAVED"
# The SoftIEC target answers with a single status byte, not with text.
SOFTIEC_OK = b"\x00"
SOFTIEC_FILE_NOT_FOUND = b"\x01"
SOFTIEC_UNKNOWN_COMMAND = b"\x04"
SOFTIEC_NOT_LOADED = b"\x05"

# Any filename bytes will do for the REU commands: the handler takes the image path
# from the "REU Preload Image" setting, not from the command. They are here only to
# push the command past the five-byte minimum length that selects the REU code path.
REU_FILENAME = b"REU.IMG"
# A path that must not exist, so LoadREU fails to open it and writes a status
# string into the reply buffer. That string is what the uppercase conversion in
# control_target.cc used to loop on forever. Absence is asserted, not assumed:
# if the file were there, a correct device would load it and report success.
ABSENT_IMAGE = "/Temp/uci_e2e_absent.reu"
# The smallest REU with a preload offset at its end. SaveREU rejects that before it
# touches storage, so SAVE_REU can be exercised on a path that writes a status
# string without renaming or writing any file.
OVERSIZE_OFFSET_SIZE = "128 KB"
OVERSIZE_OFFSET = "128 KB"
# Issue #740 reproduced the hang with an image present as well as absent. The
# reporter used a 16384-byte file, so this uses the same size. It is uploaded over
# FTP for the run and deleted afterwards.
PRESENT_IMAGE = "/Temp/uci_e2e_present.reu"
PRESENT_IMAGE_BYTES = 16384
# 128 KB REU with no preload offset, so a load of the file above transfers all of it
# and the expected byte count does not depend on the device's configured REU size.
MATRIX_REU_SIZE = "128 KB"
MATRIX_REU_OFFSET = "0 KB"
# The issue sends the filename at offset 2, and once at offset 4 behind two zero
# bytes, to rule out a framing mistake. Neither reaches the handler, which takes the
# path from the setting, but both have to complete.
FILENAME_OFFSETS = (2, 4)
# "#" selects the buffer stream, which GET_FATNAME answers with a fixed string
# without resolving a partition or touching the file system. A plain file name
# would be answered from the current SoftIEC partition, and would report an
# invalid directory on a device whose partition is not set up.
FATNAME_CHANNEL = 0x02
FATNAME_BUFFER_STREAM = b"#"
FATNAME_BUFFER_REPLY = b"/buffer"
# LOAD_SU opens a file for reading. A name that does not exist makes it report
# "file not found" without creating or changing anything. cmd_load_su() reads the
# secondary address, verify flag, load address and end address from bytes 2 to 7
# and starts the name at byte 8, so all six have to be present before the name.
LOAD_SU_MISSING = bytes(6) + b"NOSUCHFILE"

BUSY_TIMEOUT_SECONDS = 15.0
BUSY_POLL_SECONDS = 0.05
ABORT_TIMEOUT_SECONDS = 5.0
RELEASE_TIMEOUT_SECONDS = 5.0
# How often a request that can be repeated safely is retried when the device's
# connection pool is momentarily full, and how long to wait in between.
CONNECT_ATTEMPTS = 3
CONNECT_RETRY_SECONDS = 2.0
NO_REPLY_TIMEOUT_SECONDS = 5.0
MAX_QUEUE_BYTES = 1024
RESET_SETTLE_SECONDS = 3.0

TESTS = [
    "transport",
    "control-target",
    "palette",
    "issue-740-matrix",
    "save-reu-offset-past-end",
    "load-reu-disabled",
    "save-reu-disabled",
    "softiec-single-part-reply",
    "interface-usable-after",
]


class Wedged(Failure):
    """The command interface never left Command Busy: issue #740's failure mode."""


def describe_status(status: int) -> str:
    flags = [name for bit, name in STATUS_FLAGS if status & bit]
    state = STATE_NAMES[status & ST_STATE_MASK]
    return f"${status:02X} ({state}{', ' + '|'.join(flags) if flags else ''})"


def as_int32(reply: bytes) -> int:
    return int.from_bytes(reply[:4], "little", signed=True)


class RestSession:
    def __init__(self, host: str, password: str | None, timeout: float) -> None:
        self.target = targets.parse(host)
        self.host = self.target.device
        # The command interface registers are decoded on the C64 expansion bus,
        # not inside the cartridge, so the machine that reads them back has to
        # be the one driving that bus. A cartridge cannot read its own decode
        # through its own DMA cycle: measured on u2@c64u with the interface
        # enabled, $DF1B-$DF1F read back 0B 00 DD 00 1C from the cartridge and
        # 0B 00 C9 00 00 from the computer, where $DF1D = $C9 is the
        # identification byte. Only these registers move; everything else this
        # suite reads is ordinary memory and stays with the cartridge.
        self.register_host = self.target.computer
        self.password = password
        self.timeout = timeout

    def request(self, method: str, path: str, params: dict[str, object] | None = None,
                repeatable: bool = False, host: str | None = None) -> tuple[int, bytes]:
        url = f"http://{host or self.target.host_for(path)}{path}"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        headers = {"X-Password": self.password} if self.password else {}
        request = urllib.request.Request(url, headers=headers, method=method)
        # Transport and retry policy come from tests/lib/rest.py; see
        # rest.may_retry. `repeatable` is this suite's word for idempotent: a
        # register read applies nothing, so it may go again after the request
        # was sent, while a register write may not.
        try:
            with rest_lib.retrying_urlopen(request, self.timeout,
                                           idempotent=repeatable) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            # An HTTP status is an answer, not a transport failure, so it is never
            # retried. HTTPError also holds the connection open until it is
            # collected, and this suite opens one per register access, so close it
            # here rather than later.
            with exc:
                return exc.code, exc.read()
        except (OSError, TimeoutError, urllib.error.URLError) as exc:
            raise Failure(f"{method} {url} failed: {format_exception(exc)}") from exc

    def peek(self, address: int, repeatable: bool = False) -> int:
        status, body = self.request(
            "GET", READMEM_PATH, params={"address": f"{address:04x}", "length": 1},
            repeatable=repeatable, host=self.register_host
        )
        if status != 200 or len(body) != 1:
            raise Failure(f"readmem(${address:04X}) failed with HTTP {status}: {body[:200]!r}")
        return body[0]

    def poke(self, address: int, value: int) -> None:
        status, body = self.request(
            "PUT", WRITEMEM_PATH, params={"address": f"{address:04x}", "data": f"{value:02x}"},
            host=self.register_host
        )
        if status != 200:
            raise Failure(f"writemem(${address:04X}, ${value:02X}) failed with HTTP {status}: {body[:200]!r}")

    def get_config(self, category: str) -> dict[str, object]:
        status, body = self.request("GET", f"/v1/configs/{urllib.parse.quote(category)}", repeatable=True)
        if status != 200:
            raise Failure(f"GET config {category!r} failed with HTTP {status}: {body[:200]!r}")
        return json.loads(body.decode("utf-8"))[category]

    def set_config(self, category: str, item: str, value: str) -> None:
        path = f"/v1/configs/{urllib.parse.quote(category)}/{urllib.parse.quote(item)}"
        status, body = self.request("PUT", path, params={"value": value})
        if status != 200:
            raise Failure(f"PUT config {category!r}/{item!r}={value!r} failed with HTTP {status}: {body[:200]!r}")

    def file_info(self, path: str) -> dict[str, object] | None:
        quoted = urllib.parse.quote(path.lstrip("/"))
        status, body = self.request("GET", f"/v1/files/{quoted}:info", repeatable=True)
        if status == 404:
            return None
        if status != 200:
            raise Failure(f"files:info for {path!r} returned HTTP {status}: {body[:200]!r}")
        return json.loads(body.decode("utf-8"))["files"]

    def file_exists(self, path: str) -> bool:
        return self.file_info(path) is not None

    def file_size(self, path: str) -> int:
        info = self.file_info(path)
        if info is None:
            raise Failure(f"{path} does not exist on the device")
        return int(info["size"])

    def reset(self) -> None:
        status, body = self.request("PUT", RESET_PATH)
        if status != 200:
            raise Failure(f"reset failed with HTTP {status}: {body[:200]!r}")


class FtpFixture:
    """Files this suite puts on the device, over FTP because REST cannot delete."""

    def __init__(self, host: str, password: str | None, timeout: float) -> None:
        self.host = targets.device_of(host)
        self.password = password or ""
        self.timeout = timeout
        self.created: list[str] = []

    def _open(self) -> ftplib.FTP:
        return ftp_lib.connect(self.host, self.password, self.timeout)

    def _close(self, ftp: ftplib.FTP) -> None:
        ftp_lib.close(ftp)

    def upload(self, path: str, data: bytes) -> None:
        ftp = self._open()
        try:
            ftp_lib.store(ftp, path, data)
            self.created.append(path)
        except Exception as exc:
            raise Failure(f"FTP upload of {path!r} failed: {exc}") from exc
        finally:
            self._close(ftp)

    def cleanup(self) -> bool:
        """Delete what this suite created. Returns False if anything is left."""
        if not self.created:
            return True
        ok = True
        ftp = None
        try:
            ftp = self._open()
            for path in list(self.created):
                try:
                    ftp.delete(path)
                    self.created.remove(path)
                except Exception as exc:
                    warn(f"could not delete {path}: {exc}")
                    ok = False
        except Exception as exc:
            warn(f"FTP cleanup failed: {exc}")
            return False
        finally:
            if ftp is not None:
                self._close(ftp)
        return ok


class Uci:
    """The $DF1C-$DF1F transport, one REST DMA cycle per register access."""

    def __init__(self, session: RestSession, busy_timeout: float) -> None:
        self.session = session
        self.busy_timeout = busy_timeout

    def status(self) -> int:
        # Reading $DF1C has no side effect, unlike the response and status FIFOs.
        return self.session.peek(REG_CONTROL, repeatable=True)

    def control(self, bits: int) -> None:
        self.session.poke(REG_CONTROL, bits)

    def state(self) -> int:
        return self.status() & ST_STATE_MASK

    def release(self) -> bool:
        """Accept any pending data and clear a stale error, until the state is Idle.

        Accepting data from "Data More" returns to Command Busy while the target
        prepares the next block, so this has to keep trying rather than assume one
        write is enough.
        """
        deadline = time.time() + RELEASE_TIMEOUT_SECONDS
        while True:
            self.control(CTRL_DATA_ACC)
            self.control(CTRL_CLR_ERR)
            if self.state() == ST_STATE_IDLE:
                return True
            if time.time() > deadline:
                return False
            time.sleep(BUSY_POLL_SECONDS)

    def abort_to_idle(self) -> bool:
        """Ask the Ultimate to abandon the exchange, the documented way back to Idle."""
        self.control(CTRL_ABORT)
        if not self.wait_for_state(ST_STATE_IDLE, ABORT_TIMEOUT_SECONDS):
            return False
        self.control(CTRL_CLR_ERR)
        return True

    def wait_for_state(self, wanted: int, timeout: float) -> bool:
        deadline = time.time() + timeout
        while True:
            if self.state() == wanted:
                return True
            if time.time() > deadline:
                return False
            time.sleep(BUSY_POLL_SECONDS)

    def require_idle(self, when: str) -> None:
        status = self.status()
        if (status & ST_STATE_MASK) != ST_STATE_IDLE:
            raise Failure(f"command interface is not idle {when}: {describe_status(status)}")
        if status & ST_ERROR:
            raise Failure(f"command interface reports a command error {when}: {describe_status(status)}")

    def push(self, command: bytes) -> None:
        """Queue the command bytes and hand them to the Ultimate."""
        for byte in command:
            self.session.poke(REG_COMMAND, byte)
        self.control(CTRL_PUSH_CMD)

    def wait_for_reply(self, command: bytes) -> int:
        started = time.time()
        while True:
            status = self.status()
            if (status & ST_STATE_MASK) in (ST_STATE_DATA_LAST, ST_STATE_DATA_MORE):
                return status
            if time.time() - started > self.busy_timeout:
                raise Wedged(
                    f"command {command.hex(' ') or '<empty>'} never left Command Busy after "
                    f"{self.busy_timeout:.0f}s: {describe_status(status)}. The command "
                    f"interface is now wedged for every target (issue #740). "
                    f"Measured on u2@c64u, 2026-09-04: machine:reset, machine:reboot "
                    f"and injected keys do not release it, a power cycle always does, "
                    f"and it came back once after a runners:run_prg on the same "
                    f"machine. Try run_prg first, it is much cheaper."
                )
            time.sleep(BUSY_POLL_SECONDS)

    def drain(self) -> tuple[bytes, bytes]:
        return (bytes(self._drain(ST_DATA_AV, REG_RESPONSE, "response")),
                bytes(self._drain(ST_STAT_AV, REG_STATUS, "status")))

    def _drain(self, available_bit: int, register: int, what: str) -> bytearray:
        out = bytearray()
        while self.status() & available_bit:
            out.append(self.session.peek(register))
            if len(out) > MAX_QUEUE_BYTES:
                raise Failure(f"{what} queue did not drain within {MAX_QUEUE_BYTES} bytes: {bytes(out)[:80]!r}")
        return out

    def transact(self, command: bytes) -> tuple[bytes, bytes]:
        """Push one command and return (response data, status text).

        Every command this suite sends is answered in one part, so the reply has to
        arrive in the Data Last state. A single-part reply announced as Data More
        makes a client that follows the protocol accept the data and then wait for a
        block that never comes.
        """
        self.release()
        self.require_idle("before pushing a command")
        started = time.time()
        self.push(command)
        status = self.wait_for_reply(command)
        elapsed = time.time() - started

        if (status & ST_STATE_MASK) != ST_STATE_DATA_LAST:
            raise Failure(
                f"command {command.hex(' ')} replied in state {describe_status(status)}; "
                f"this reply is sent in one part, so the state has to be Data Last"
            )

        data, text = self.drain()
        self.control(CTRL_DATA_ACC)
        self.require_idle("after accepting the reply")
        detail(f"{command.hex(' ') or '<empty>'} -> {elapsed:.2f}s, data {data!r}, status {text!r}")
        return data, text


def expect(uci: Uci, label: str, command: bytes, status: bytes,
           reply: bytes | None = None, reply_prefix: bytes | None = None) -> bytes:
    """Run one command and check the documented status and reply."""
    with check(label):
        got_reply, got_status = uci.transact(command)
        if got_status != status:
            raise Failure(f"{label}: expected status {status!r}, got {got_status!r}")
        if reply is not None and got_reply != reply:
            raise Failure(f"{label}: expected reply {reply!r}, got {got_reply!r}")
        if reply_prefix is not None and not got_reply.startswith(reply_prefix):
            raise Failure(f"{label}: expected a reply starting with {reply_prefix!r}, got {got_reply!r}")
        return got_reply


def expect_uppercase(message: str, scenario: str) -> None:
    """The whole reply text must be uppercased, not just its first character.

    The conversion in control_target.cc used to re-read the same byte forever
    instead of advancing, so it never got past character one.
    """
    lowercase = [c for c in message if c.islower()]
    if lowercase:
        raise Failure(
            f"{scenario}: reply text was not fully uppercased, "
            f"{len(lowercase)} lowercase character(s) in {message!r}"
        )


def run_transport(uci: Uci) -> bool:
    """The transport state machine of the Register API, independent of any target."""
    scenario = "transport"

    # run_task() handles a zero-length command itself: it answers with an empty data
    # block instead of handing anything to a target.
    expect(uci, f"{scenario}: an empty command returns an empty reply", b"", b"", reply=b"")

    # Nothing registers on target $0F, so the dispatcher's placeholder answers.
    expect(uci, f"{scenario}: unregistered target answers IDENTIFY",
           bytes([TARGET_UNREGISTERED, CTRL_CMD_IDENTIFY]), STATUS_OK, reply=b"NO TARGET")
    expect(uci, f"{scenario}: unregistered target rejects anything else",
           bytes([TARGET_UNREGISTERED, CMD_UNIMPLEMENTED]), STATUS_UNKNOWN_COMMAND, reply=b"")

    with check(f"{scenario}: a no-reply command returns straight to Idle"):
        uci.release()
        uci.require_idle("before pushing a no-reply command")
        uci.push(bytes([TARGET_CONTROL | TARGET_NO_REPLY, CTRL_CMD_IDENTIFY]))
        if not uci.wait_for_state(ST_STATE_IDLE, NO_REPLY_TIMEOUT_SECONDS):
            raise Failure(
                f"{scenario}: expected Idle within {NO_REPLY_TIMEOUT_SECONDS:.0f}s of a "
                f"no-reply command, got {describe_status(uci.status())}"
            )
        status = uci.status()
        if status & (ST_DATA_AV | ST_STAT_AV | ST_ERROR):
            raise Failure(
                f"{scenario}: a no-reply command left data, status or an error behind: "
                f"{describe_status(status)}"
            )

    with check(f"{scenario}: pushing while a reply is pending sets and clears ERROR"):
        uci.release()
        uci.push(bytes([TARGET_CONTROL, CTRL_CMD_IDENTIFY]))
        uci.wait_for_reply(b"\x04\x01")
        # Strobe PUSH_CMD on its own. Queueing command bytes first would leave them
        # in the command queue when the push is rejected, and the interface only
        # resets the data and status queues, not that one.
        uci.control(CTRL_PUSH_CMD)
        status = uci.status()
        if not (status & ST_ERROR):
            raise Failure(
                f"{scenario}: pushing a command while the interface was not idle should set "
                f"ERROR, got {describe_status(status)}"
            )
        uci.control(CTRL_CLR_ERR)
        status = uci.status()
        if status & ST_ERROR:
            raise Failure(f"{scenario}: CLR_ERR did not clear the error: {describe_status(status)}")
        # The reply that was already there has to survive the rejected push.
        data, text = uci.drain()
        uci.control(CTRL_DATA_ACC)
        if not data.startswith(b"CONTROL TARGET") or text != STATUS_OK:
            raise Failure(f"{scenario}: the pending reply was damaged: data {data!r}, status {text!r}")
        uci.require_idle("after recovering from a state error")

    with check(f"{scenario}: the next command is not prefixed by leftover bytes"):
        # If anything were left in the command queue, this GET_HWINFO would be read
        # as the IDENTIFY in front of it and would answer with the target name.
        reply, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_GET_HWINFO, 0x00]))
        if text != STATUS_OK:
            raise Failure(f"{scenario}: expected status {STATUS_OK!r}, got {text!r}")
        if not reply or reply.startswith(b"CONTROL TARGET"):
            raise Failure(
                f"{scenario}: GET_HWINFO answered {reply!r}, which is the reply to the "
                f"preceding IDENTIFY; the command queue still held it"
            )

    with check(f"{scenario}: ABORT releases a pending reply back to Idle"):
        uci.release()
        uci.push(bytes([TARGET_CONTROL, CTRL_CMD_IDENTIFY]))
        uci.wait_for_reply(b"\x04\x01")
        if not uci.abort_to_idle():
            raise Failure(
                f"{scenario}: the interface did not return to Idle within "
                f"{ABORT_TIMEOUT_SECONDS:.0f}s of an abort: {describe_status(uci.status())}"
            )

    return True


def run_control_target(uci: Uci) -> bool:
    """Control target identification and the commands it has to reject."""
    scenario = "control-target"
    expect(uci, f"{scenario}: IDENTIFY answers with the target name",
           bytes([TARGET_CONTROL, CTRL_CMD_IDENTIFY]), STATUS_OK, reply_prefix=b"CONTROL TARGET")
    expect(uci, f"{scenario}: an unimplemented command is reported as unknown",
           bytes([TARGET_CONTROL, CMD_UNIMPLEMENTED]), STATUS_UNKNOWN_COMMAND, reply=b"")
    # Both REU commands share one handler that needs at least five command bytes.
    expect(uci, f"{scenario}: LOAD_REU without a filename is rejected, not run",
           bytes([TARGET_CONTROL, CTRL_CMD_LOAD_REU]), STATUS_INVALID_PARAMS, reply=b"")
    expect(uci, f"{scenario}: SAVE_REU without a filename is rejected, not run",
           bytes([TARGET_CONTROL, CTRL_CMD_SAVE_REU]), STATUS_INVALID_PARAMS, reply=b"")
    expect(uci, f"{scenario}: GET_HWINFO rejects a device number it does not have",
           bytes([TARGET_CONTROL, CTRL_CMD_GET_HWINFO, 0x02]), STATUS_INVALID_PARAMS, reply=b"")
    return True


def run_palette(uci: Uci) -> bool:
    """Exercise the U64 runtime palette protocol without changing saved config."""
    scenario = "palette"
    product, status = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_GET_HWINFO, 0x00]))
    if status != STATUS_OK:
        raise Failure(f"{scenario}: could not identify the product: {status!r}")
    if not product.startswith(b"Ultimate 64"):
        detail(f"{scenario}: {product.decode('latin-1')} has no U64 palette hardware; skipped")
        return True

    original = expect(
        uci, f"{scenario}: GET_PALETTE returns 16 RGB colors",
        bytes([TARGET_CONTROL, CTRL_CMD_GET_PALETTE]), STATUS_OK)
    if len(original) != 48:
        raise Failure(f"{scenario}: expected 48 palette bytes, got {len(original)}")

    try:
        with check(f"{scenario}: SET_PALETTE_COLOR changes only the requested color"):
            changed = bytearray(original)
            changed[-3:] = bytes(component ^ 0x5A for component in changed[-3:])
            command = bytes([TARGET_CONTROL, CTRL_CMD_SET_PALETTE_COLOR, 15]) + changed[-3:]
            reply, text = uci.transact(command)
            if text != STATUS_OK or reply:
                raise Failure(f"{scenario}: single-color set returned data {reply!r}, status {text!r}")
            actual, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_GET_PALETTE]))
            if text != STATUS_OK or actual != bytes(changed):
                raise Failure(f"{scenario}: single-color readback was {actual!r}, expected {bytes(changed)!r}")

        expect(uci, f"{scenario}: color index 16 is rejected",
               bytes([TARGET_CONTROL, CTRL_CMD_SET_PALETTE_COLOR, 16, 0, 0, 0]),
               STATUS_INVALID_PARAMS, reply=b"")

        with check(f"{scenario}: SET_PALETTE replaces all 16 RGB colors"):
            replacement = bytes((i * 37 + 11) & 0xFF for i in range(48))
            reply, text = uci.transact(
                bytes([TARGET_CONTROL, CTRL_CMD_SET_PALETTE]) + replacement)
            if text != STATUS_OK or reply:
                raise Failure(f"{scenario}: full set returned data {reply!r}, status {text!r}")
            actual, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_GET_PALETTE]))
            if text != STATUS_OK or actual != replacement:
                raise Failure(f"{scenario}: full-palette readback was {actual!r}, expected {replacement!r}")

        expect(uci, f"{scenario}: short SET_PALETTE payload is rejected",
               bytes([TARGET_CONTROL, CTRL_CMD_SET_PALETTE]) + original[:-1],
               STATUS_INVALID_PARAMS, reply=b"")
        expect(uci, f"{scenario}: GET_PALETTE rejects a payload",
               bytes([TARGET_CONTROL, CTRL_CMD_GET_PALETTE, 0]),
               STATUS_INVALID_PARAMS, reply=b"")

        with check(f"{scenario}: RESET_PALETTE restores the built-in C64 colors"):
            default_palette = bytes.fromhex(
                "000000 f7f7f7 8d2f34 6ad4cd 9835a4 4cb442 2c29b1 efef5d "
                "984e20 5b3800 d1676d 4a4a4a 7b7b7b 9fef93 6d6aef b2b2b2")
            reply, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_RESET_PALETTE]))
            if text != STATUS_OK or reply:
                raise Failure(f"{scenario}: reset returned data {reply!r}, status {text!r}")
            actual, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_GET_PALETTE]))
            if text != STATUS_OK or actual != default_palette:
                raise Failure(f"{scenario}: reset palette readback was {actual!r}, expected {default_palette!r}")

        expect(uci, f"{scenario}: RESET_PALETTE rejects a payload",
               bytes([TARGET_CONTROL, CTRL_CMD_RESET_PALETTE, 0]),
               STATUS_INVALID_PARAMS, reply=b"")
    finally:
        with check(f"{scenario}: restore the original runtime palette"):
            reply, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_SET_PALETTE]) + original)
            if text != STATUS_OK or reply:
                raise Failure(f"{scenario}: restore returned data {reply!r}, status {text!r}")
            actual, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_GET_PALETTE]))
            if text != STATUS_OK or actual != original:
                raise Failure(f"{scenario}: restored palette readback was {actual!r}")
    return True


def prime_reply_buffer(uci: Uci) -> None:
    """Leave a non-empty string in the control target's reply buffer.

    CTRL_CMD_GET_HWINFO writes the product name there. The REU commands reuse the
    same buffer, and the handler does not write a status string of its own when the
    REU is disabled, so this is what makes the "no status written" path observable
    rather than dependent on whatever the heap happened to contain.
    """
    reply, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_GET_HWINFO, 0x00]))
    if text != STATUS_OK:
        raise Failure(f"prime reply buffer: expected status {STATUS_OK!r}, got {text!r}")
    if len(reply) < 5 or reply[4] == 0:
        raise Failure(
            f"prime reply buffer: GET_HWINFO returned {reply!r}, which leaves no text at "
            f"offset 4; the following REU checks would not be meaningful"
        )


def load_reu_command(filename_offset: int) -> bytes:
    """The LOAD_REU command as issue #740 sends it, with the name at offset 2 or 4."""
    padding = bytes(filename_offset - 2)
    return bytes([TARGET_CONTROL, CTRL_CMD_LOAD_REU]) + padding + REU_FILENAME


def run_issue_740_matrix(session: RestSession, ftp: "FtpFixture", uci: Uci) -> bool:
    """Walk the four conditions issue #740 reported, and check each one completes.

    The report lists REU enabled and disabled, image present and absent, and the
    filename at offset 2 and at offset 4. All four hung identically. Each row here
    sends the same command the report sends and requires the interface to leave
    Command Busy, return the status that row should produce, and go back to Idle on
    a single DATA_ACC, with no abort and no clear-error needed.
    """
    scenario = "issue-740-matrix"

    with check(f"{scenario}: confirm {ABSENT_IMAGE} does not exist"):
        if session.file_exists(ABSENT_IMAGE):
            raise Failure(
                f"{scenario} needs {ABSENT_IMAGE} to be absent so the load fails to open it, "
                f"but the file exists on this device. Delete it and re-run."
            )
    with check(f"{scenario}: put a {PRESENT_IMAGE_BYTES}-byte image at {PRESENT_IMAGE}"):
        ftp.upload(PRESENT_IMAGE, bytes(PRESENT_IMAGE_BYTES))
        actual = session.file_size(PRESENT_IMAGE)
        if actual != PRESENT_IMAGE_BYTES:
            raise Failure(f"{scenario}: uploaded image is {actual} bytes, expected {PRESENT_IMAGE_BYTES}")
        session.set_config(CONFIG_CATEGORY, CFG_REU_SIZE, MATRIX_REU_SIZE)
        session.set_config(CONFIG_CATEGORY, CFG_REU_OFFSET, MATRIX_REU_OFFSET)

    # (REU setting, image path, expected status, expected transferred count, text prefix)
    rows = [
        ("Enabled", ABSENT_IMAGE, STATUS_REU_CANNOT_OPEN, -1, "REU LOAD: FAILED TO OPEN "),
        ("Disabled", ABSENT_IMAGE, STATUS_REU_DISABLED, -2, None),
        ("Enabled", PRESENT_IMAGE, STATUS_OK, PRESENT_IMAGE_BYTES, "REU LOAD: LOADED "),
    ]
    for reu, image, want_status, want_count, want_prefix in rows:
        for offset in FILENAME_OFFSETS:
            label = f"REU {reu}, image {'present' if image == PRESENT_IMAGE else 'absent'}, name at offset {offset}"
            with check(f"{scenario}: {label}"):
                session.set_config(CONFIG_CATEGORY, CFG_REU_ENABLE, reu)
                session.set_config(CONFIG_CATEGORY, CFG_REU_IMAGE, image)
                command = load_reu_command(offset)
                # transact() fails with the wedge message if the interface stays in
                # Command Busy, and requires Idle after a single DATA_ACC.
                reply, text = uci.transact(command)
                if text != want_status:
                    raise Failure(f"{scenario} [{label}]: expected status {want_status!r}, got {text!r}")
                if as_int32(reply) != want_count:
                    raise Failure(
                        f"{scenario} [{label}]: expected {want_count} bytes transferred, "
                        f"got {as_int32(reply)} from {reply!r}"
                    )
                message = reply[4:].decode("latin-1")
                if want_prefix is None:
                    # The handler writes no status string when the REU is disabled.
                    if reply[4:]:
                        raise Failure(
                            f"{scenario} [{label}]: expected a 4-byte reply, got {len(reply)} bytes "
                            f"{reply!r} (stale text from the previous command)"
                        )
                else:
                    if not message.startswith(want_prefix):
                        raise Failure(f"{scenario} [{label}]: unexpected reply text {message!r}")
                    if image.upper() not in message:
                        raise Failure(
                            f"{scenario} [{label}]: reply text does not name {image.upper()!r}: {message!r}"
                        )
                    expect_uppercase(message, f"{scenario} [{label}]")
    return True


def run_save_reu_offset_past_end(session: RestSession, uci: Uci) -> bool:
    """Exercise SAVE_REU on a path that writes its own status string.

    A save with the preload offset at the end of the REU is rejected before any
    rename, delete or write happens, so this covers the same uppercase conversion
    for SAVE_REU without producing a multi-megabyte file on the device.
    """
    scenario = "save-reu-offset-past-end"
    with check(f"{scenario}: put the preload offset at the end of a 128 KB REU"):
        session.set_config(CONFIG_CATEGORY, CFG_REU_ENABLE, "Enabled")
        session.set_config(CONFIG_CATEGORY, CFG_REU_SIZE, OVERSIZE_OFFSET_SIZE)
        session.set_config(CONFIG_CATEGORY, CFG_REU_OFFSET, OVERSIZE_OFFSET)
    with check(f"{scenario}: SAVE_REU reports the offset and saves nothing"):
        reply, text = uci.transact(bytes([TARGET_CONTROL, CTRL_CMD_SAVE_REU]) + REU_FILENAME)
        if text != STATUS_REU_NOT_SAVED:
            raise Failure(f"{scenario}: expected status {STATUS_REU_NOT_SAVED!r}, got {text!r}")
        if as_int32(reply) != -3:
            raise Failure(f"{scenario}: expected -3 transferred, got {as_int32(reply)} from {reply!r}")
        message = reply[4:].decode("latin-1")
        if not message.startswith("REU SAVE: OFFSET "):
            raise Failure(f"{scenario}: unexpected reply text {message!r}")
        expect_uppercase(message, scenario)
    return True


def run_reu_disabled(session: RestSession, uci: Uci, command: int, scenario: str) -> bool:
    """Run LOAD_REU or SAVE_REU with the REU switched off.

    SAVE_REU is only exercised here and in save-reu-offset-past-end, and never
    against an REU that would really be written, because a real save renames the
    existing image and writes the full REU size to storage.
    """
    with check(f"{scenario}: disable the REU"):
        session.set_config(CONFIG_CATEGORY, CFG_REU_ENABLE, "Disabled")
    with check(f"{scenario}: leave text in the reply buffer from a previous command"):
        prime_reply_buffer(uci)
    with check(f"{scenario}: command reports the REU is disabled and replies with 4 bytes"):
        reply, text = uci.transact(bytes([TARGET_CONTROL, command]) + REU_FILENAME)
        if text != STATUS_REU_DISABLED:
            raise Failure(f"{scenario}: expected status {STATUS_REU_DISABLED!r}, got {text!r}")
        # The handler writes no status string on this path, so the reply is the
        # 4-byte transferred count and nothing else. Trailing bytes here mean the
        # previous command's text was measured as this command's reply.
        if len(reply) != 4:
            raise Failure(
                f"{scenario}: expected a 4-byte reply, got {len(reply)} bytes {reply!r} "
                f"(stale text from the previous command)"
            )
        if as_int32(reply) != -2:
            raise Failure(f"{scenario}: expected -2 transferred, got {as_int32(reply)} from {reply!r}")
    return True


def run_softiec_single_part_reply(uci: Uci) -> bool:
    """The SoftIEC target sends every reply in one part, so it must say Data Last.

    Both commands used here are read-only: LOAD_SU on a name that does not exist
    reports "file not found" and GET_FATNAME only asks what a name maps to. They
    cover the empty and the non-empty reply through the same reply buffer.
    """
    scenario = "softiec-single-part-reply"
    with check(f"{scenario}: SoftIEC target answers IDENTIFY"):
        reply, text = uci.transact(bytes([TARGET_SOFTIEC, SOFTIEC_CMD_IDENTIFY]))
        if text == SOFTIEC_NOT_LOADED:
            raise Failure("the SoftIEC drive is not enabled on this device, so this check cannot run")
        if text != STATUS_OK:
            raise Failure(f"{scenario}: expected status {STATUS_OK!r}, got {text!r}")
        if not reply.startswith(b"SOFTWARE IEC TARGET"):
            raise Failure(f"{scenario}: unexpected identification {reply!r}")
    expect(uci, f"{scenario}: LOAD_SU on a missing file completes in one part",
           bytes([TARGET_SOFTIEC, SOFTIEC_CMD_LOAD_SU]) + LOAD_SU_MISSING,
           SOFTIEC_FILE_NOT_FOUND, reply=b"")
    expect(uci, f"{scenario}: GET_FATNAME reply completes in one part",
           bytes([TARGET_SOFTIEC, SOFTIEC_CMD_GET_FATNAME, FATNAME_CHANNEL]) + FATNAME_BUFFER_STREAM,
           SOFTIEC_OK, reply=FATNAME_BUFFER_REPLY)
    expect(uci, f"{scenario}: an unimplemented SoftIEC command is reported as unknown",
           bytes([TARGET_SOFTIEC, CMD_UNIMPLEMENTED]), SOFTIEC_UNKNOWN_COMMAND, reply=b"")
    return True


def run_interface_usable_after(uci: Uci) -> bool:
    expect(uci, "interface-usable-after: the control target still answers IDENTIFY",
           bytes([TARGET_CONTROL, CTRL_CMD_IDENTIFY]), STATUS_OK, reply_prefix=b"CONTROL TARGET")
    return True


def release_interface(uci: Uci) -> bool:
    """Leave the command interface idle and error-free, whatever the scenarios did.

    Accepting the data is tried first because it is the ordinary way to finish an
    exchange; an abort is the documented fallback that forces the state machine back
    to Idle when a reply cannot be drained.
    """
    try:
        if not uci.release():
            uci.abort_to_idle()
        status = uci.status()
        if (status & ST_STATE_MASK) != ST_STATE_IDLE or (status & ST_ERROR):
            warn(f"command interface left in {describe_status(status)}, "
                 f"wanted Idle with no error")
            return False
        return True
    except Exception as exc:
        warn(f"could not release the command interface: {exc}")
        return False


def restore_settings(session: RestSession, original: dict[str, str], keep_config: bool) -> bool:
    """Put every setting the suite wrote back, and prove it took effect."""
    if not original or keep_config:
        return True
    try:
        for item, value in original.items():
            session.set_config(CONFIG_CATEGORY, item, value)
        current = session.get_config(CONFIG_CATEGORY)
        wrong = {k: str(current.get(k)) for k, v in original.items() if str(current.get(k)) != v}
        if wrong:
            warn(f"settings not restored, still {wrong}, wanted {original}")
            return False
        detail(f"restored {CONFIG_CATEGORY}: {original}")
        return True
    except Exception as exc:
        warn(f"cleanup failed: {exc}")
        return False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify the UCI transport state machine, that CTRL_CMD_LOAD_REU / "
                    "CTRL_CMD_SAVE_REU complete and leave the interface usable (issue #740), "
                    "and that SoftIEC replies are framed as single-part."
    )
    cli.add_device_arguments(parser, password=None, timeout=30.0, colour=False)
    parser.add_argument("-b", "--busy-timeout", type=float, default=BUSY_TIMEOUT_SECONDS,
                        help="How long a single command may stay in Command Busy before it counts as wedged.")
    parser.add_argument("--test", action="append", choices=["all", *TESTS])
    parser.add_argument("--no-reset", action="store_true",
                        help="Skip resetting the C64 before the test (use an already-stable machine).")
    parser.add_argument("--keep-config", action="store_true",
                        help="Don't restore the original Command Interface and REU settings on exit.")
    args = parser.parse_args()

    selected = TESTS if not args.test or "all" in args.test else [t for t in TESTS if t in args.test]
    session = RestSession(args.host, args.password, args.timeout)
    ftp = FtpFixture(args.host, args.password, args.timeout)
    uci = Uci(session, args.busy_timeout)

    original: dict[str, str] = {}
    results: dict[str, bool] = {}
    cleanup_ok = True
    # $DF1C only belongs to the command interface once the setting is on; before
    # that the address is the REU's, so the suite must not write to it.
    interface_enabled = False

    def run(name: str, fn, *fn_args) -> None:
        if name not in selected:
            return
        results[name] = False  # so an aborted scenario reports FAIL, not "not reached"
        results[name] = fn(*fn_args)

    try:
        if not args.no_reset:
            with check("reset the C64 so the command interface starts idle"):
                session.reset()
                time.sleep(RESET_SETTLE_SECONDS)

        with check(f"read {CONFIG_CATEGORY!r}"):
            config = session.get_config(CONFIG_CATEGORY)
            missing = [k for k in OWNED_SETTINGS if k not in config]
            if missing:
                raise Failure(f"this device has no {', '.join(missing)} setting; it cannot run this suite")
            original = {k: str(config[k]) for k in OWNED_SETTINGS}
            detail(f"current: {original}")

        with check("enable the Command Interface registers at $DF1B-$DF1F"):
            session.set_config(CONFIG_CATEGORY, CFG_CMD_IF, "Enabled")
            interface_enabled = True
            uci.release()

        # Asked of the machine rather than assumed from the setting: measured
        # on a C64 Ultimate 1.2.0, the setting reads "Enabled" while the whole
        # register window reads $FF, which is the bus floating because nothing
        # answers there. An Ultimate 64 answers $02 $FF $02 $02 $02 at the
        # same five addresses even with the setting off. Every check below
        # drives those registers, so there is nothing to test where they are
        # not present.
        if all(session.peek(0xDF1B + offset) == 0xFF for offset in range(5)):
            check_start("this machine answers at the Command Interface registers")
            check_skip("$DF1B-$DF1F all read $FF, so no command interface is "
                       "present at those addresses on this machine")
            suite_ok("uci_targets_test")
            return 0

        with check("the command interface is idle once enabled"):
            uci.require_idle("after enabling the command interface")
            identification = session.peek(REG_COMMAND)
            if identification not in (0xC9, 0x49):
                raise Failure(
                    f"$DF1D read back ${identification:02X}, expected the command interface "
                    f"identification $C9 (or $49 while an interrupt is pending)"
                )

        run("transport", run_transport, uci)
        run("control-target", run_control_target, uci)
        run("palette", run_palette, uci)
        run("issue-740-matrix", run_issue_740_matrix, session, ftp, uci)
        run("save-reu-offset-past-end", run_save_reu_offset_past_end, session, uci)
        run("load-reu-disabled", run_reu_disabled, session, uci, CTRL_CMD_LOAD_REU, "load-reu-disabled")
        run("save-reu-disabled", run_reu_disabled, session, uci, CTRL_CMD_SAVE_REU, "save-reu-disabled")
        run("softiec-single-part-reply", run_softiec_single_part_reply, uci)
        run("interface-usable-after", run_interface_usable_after, uci)

    except Failure as exc:
        suite_fail("uci_targets_test", format_exception(exc))
    finally:
        # A failed scenario can leave the interface holding a reply, so hand the
        # data back before the settings go home. Both steps have to succeed, and
        # both run even if the first one fails.
        released = release_interface(uci) if interface_enabled else True
        restored = restore_settings(session, original, args.keep_config)
        removed = ftp.cleanup()
        cleanup_ok = released and restored and removed

    section("summary")
    all_ok = cleanup_ok
    for name in selected:
        outcome = results.get(name)
        state = OK if outcome else (FAIL if outcome is False else SKIP)
        if outcome is not True:
            all_ok = False
        detail(f"{name}: {state}")
    detail(f"cleanup: {OK if cleanup_ok else FAIL}")

    if all_ok:
        suite_ok("uci_targets_test")
        return 0
    suite_fail("uci_targets_test", "see the summary above")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Failure as exc:
        suite_fail("uci_targets_test", format_exception(exc))
        raise SystemExit(1)
