#!/usr/bin/env python3
"""A simulated network target, for checking the UCI TLS suite without hardware.

Two flavours, each a `net_tls_proto.Link`:

  "tls"    answers $21-$25 the way doc/uci_tls.md says, doing
           real TLS from this machine with Python's ssl module, so a scenario
           that passes against it has exercised its own oracle end to end;
  "stock"  the firmware without the TLS commands: $21-$25 answer `21,UNKNOWN COMMAND`, and
           the plain commands ($01, $07, $09, $10, $11) work.

And mutants of "tls", each breaking one rule a scenario claims to guard, so
the self-test can show that scenario turning red:

  "bundle-accepts-all"  BUNDLE verifies nothing (S5 / S4 must catch it)
  "release-noop"        $25 never closes anything (S19e must catch it)
  "group-zero"          the OPEN reply's group is 0 (S2 must catch it)
  "info-ff-claims"      `03 23 FF` claims every session (S19e2b must catch it)
  "no-notify-reply"     the peer's close_notify is not answered with ours (S6 must catch it)
  "abort-shortens"      an ABORT ends PROCESSING early: bit 2 clears at the ABORT,
                        while the open still completes behind it (S19e must catch it)

It is a model of the contract, not of the firmware: timings are not
simulated, a READ takes what the socket has, and the group is reported as
x25519 because Python's ssl does not expose the negotiated one. Nothing here
is evidence about a device.
"""

import hashlib
import ipaddress
import socket
import ssl
import time
from dataclasses import dataclass

from net_tls_proto import (
    CA_MAX, HANDLE_RANGE, NET_CMD_CLOSE_SOCKET, NET_CMD_IDENTIFY, NET_CMD_OPEN_TCP,
    NET_CMD_OPEN_TLS, NET_CMD_READ_SOCKET, NET_CMD_START_TLS, NET_CMD_TLS_INFO,
    NET_CMD_TLS_RELEASE, NET_CMD_TLS_TRUST_CA, NET_CMD_WRITE_SOCKET, READ_MAX,
    STATUS_INVALID_PARAMS, STATUS_OK, STATUS_OUT_OF_RANGE, STATUS_UNKNOWN_COMMAND,
    TLS_COMMANDS, TRUST_BUNDLE, TRUST_CUSTOM, TRUST_PIN, AbortedCommand, Link, Result)

try:
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization
except ImportError:  # pragma: no cover
    x509 = None

MUTANTS = ("bundle-accepts-all", "release-noop", "group-zero", "info-ff-claims",
           "abort-shortens", "no-notify-reply")
FLAVOURS = ("tls", "stock", *MUTANTS)
MAX_TLS = 2

SUITES = {
    "TLS_AES_128_GCM_SHA256": 0x1301, "TLS_AES_256_GCM_SHA384": 0x1302,
    "TLS_CHACHA20_POLY1305_SHA256": 0x1303,
    "ECDHE-ECDSA-AES128-GCM-SHA256": 0xC02B, "ECDHE-ECDSA-AES256-GCM-SHA384": 0xC02C,
    "ECDHE-RSA-AES128-GCM-SHA256": 0xC02F, "ECDHE-RSA-AES256-GCM-SHA384": 0xC030,
    "ECDHE-ECDSA-CHACHA20-POLY1305": 0xCCA9, "ECDHE-RSA-CHACHA20-POLY1305": 0xCCA8,
}
# The six hardened 1.2 suites of 1.7, as OpenSSL names them.
TLS12_CIPHERS = ":".join(name for name, code in SUITES.items() if code >= 0xC000)
ALERTS = {"PROTOCOL_VERSION": 70, "HANDSHAKE_FAILURE": 40, "INTERNAL_ERROR": 80,
          "DECODE_ERROR": 50, "ILLEGAL_PARAMETER": 47, "UNEXPECTED_MESSAGE": 10}


def is_ipv4_literal(text: bytes) -> bool:
    """lwIP's ip4addr_aton is laxer than this (127.1, 0x7f.0.0.1); the mock uses
    inet_aton, which accepts the same short and hex forms."""
    try:
        socket.inet_aton(text.decode("ascii"))
        return True
    except (OSError, UnicodeDecodeError):
        return False


def status(text: str) -> bytes:
    return text.encode("ascii")


@dataclass
class Entry:
    kind: str                # "tcp" or "tls"
    sock: object
    claimed: bool = True
    open_reply: bytes = b""
    trust: int = 0
    spki: bytes = b""
    handshake_ms: int = 0
    sticky: bytes | None = None


class MockDevice(Link):
    """A network target in Python. See the module docstring."""

    def __init__(self, flavour: str = "tls", bundle_cafile: str | None = None,
                 resolver: dict[str, str] | None = None, system_bundle: bool = True) -> None:
        super().__init__()
        if flavour not in FLAVOURS:
            raise ValueError(f"unknown flavour {flavour!r}")
        self.flavour = flavour
        self.name = f"mock-{flavour}"
        self.can_abort_in_flight = True
        self.bundle_cafile = bundle_cafile
        self.system_bundle = system_bundle
        self.resolver = resolver or {}
        self.handles: dict[int, Entry] = {}
        self.ca_state = "EMPTY"
        self.ca_buf = bytearray()
        self.ca_total = 0
        self.ca_der: bytes | None = None
        self.ready = 0x07          # module, entropy, time

    # -- Link ----------------------------------------------------------------

    def _command(self, command: bytes, bound: float) -> Result:
        started = time.monotonic()
        data, text = self._dispatch(command)
        return Result(command, data, text, time.monotonic() - started)

    def abort_in_flight(self, command: bytes, abort_after: float,
                        bound: float | None = None) -> AbortedCommand:
        # ABORT never shortens a command: it runs to its end, and only
        # its reply is dropped.
        started = time.monotonic()
        self._dispatch(command)
        elapsed = time.monotonic() - started
        if self.flavour == "abort-shortens":
            elapsed = abort_after
        self.log.append({"t": round(time.time(), 3), "link": self.name,
                         "command": command.hex(" "), "abort_after": abort_after,
                         "busy_at_abort": True, "bit2_cleared_after": round(elapsed, 3)})
        return AbortedCommand(command, abort_after, True, elapsed, 0x00)

    def startup_abort(self) -> float:
        return 0.0

    def reset(self) -> None:
        """A C64 reset: every socket closed, CA EMPTY."""
        for entry in self.handles.values():
            try:
                entry.sock.close()
            except OSError:
                pass
        self.handles.clear()
        self.ca_state, self.ca_der = "EMPTY", None

    # -- dispatch ------------------------------------------------------------

    def _dispatch(self, command: bytes) -> tuple[bytes, bytes]:
        if len(command) < 2:
            return b"", STATUS_INVALID_PARAMS
        code = command[1]
        if self.flavour == "stock" and code in TLS_COMMANDS:
            return b"", STATUS_UNKNOWN_COMMAND
        if command[0] & 0x80:
            # The no-reply bit: the reply is thrown away (command_intf.cc:166-171),
            # and $21/$24 open nothing (r6 L-i).
            if code in (NET_CMD_OPEN_TLS, NET_CMD_START_TLS):
                return b"", STATUS_INVALID_PARAMS
        exempt = code == NET_CMD_TLS_RELEASE or (
            code == NET_CMD_TLS_INFO and len(command) == 3 and command[2] == 0xFF
            and self.flavour != "info-ff-claims")
        if self.flavour != "stock" and not exempt:
            for entry in self.handles.values():
                entry.claimed = True
        if code != NET_CMD_TLS_TRUST_CA and self.ca_state == "UPLOADING":
            self.ca_state = "EMPTY"
        handler = {
            NET_CMD_IDENTIFY: self._identify, NET_CMD_OPEN_TCP: self._open_tcp,
            NET_CMD_CLOSE_SOCKET: self._close, NET_CMD_READ_SOCKET: self._read,
            NET_CMD_WRITE_SOCKET: self._write, NET_CMD_OPEN_TLS: self._open_tls,
            NET_CMD_TLS_TRUST_CA: self._trust_ca, NET_CMD_TLS_INFO: self._info,
            NET_CMD_TLS_RELEASE: self._release,
        }.get(code)
        if handler is None:
            return b"", STATUS_UNKNOWN_COMMAND
        return handler(command)

    def _free_handle(self) -> int | None:
        return next((h for h in HANDLE_RANGE if h not in self.handles), None)

    def _identify(self, command: bytes):
        return b"MOCK NETWORK TARGET", STATUS_OK

    def _resolve(self, host: str) -> str | None:
        if host in self.resolver:
            return self.resolver[host]
        try:
            ipaddress.IPv4Address(host)
            return host
        except ValueError:
            pass
        try:
            return socket.gethostbyname(host)
        except OSError:
            return None

    def _open_tcp(self, command: bytes):
        if len(command) < 5:
            return b"", STATUS_INVALID_PARAMS
        port = command[2] | command[3] << 8
        host = command[4:].split(b"\x00")[0].decode("latin-1")
        address = self._resolve(host)
        if address is None:
            return b"", status("84,UNRESOLVED HOST")
        handle = self._free_handle()
        if handle is None:
            return b"", status("85,ERROR OPENING SOCKET")
        try:
            sock = socket.create_connection((address, port), timeout=10)
        except OSError as exc:
            return b"", status(f"11,ERROR ON CONNECT: {exc.errno or 116}")
        self.handles[handle] = Entry("tcp", sock)
        return bytes([handle]), STATUS_OK

    def _close(self, command: bytes):
        if len(command) != 3:
            return b"", STATUS_INVALID_PARAMS
        entry = self.handles.pop(command[2], None)
        if entry is None:
            return b"", status("12,ERROR ON CLOSE: 9")
        if entry.kind == "tls":
            try:
                entry.sock.settimeout(0.1)
                entry.sock.unwrap()
            except (OSError, ValueError, ssl.SSLError):
                pass
        try:
            entry.sock.close()
        except OSError:
            pass
        return b"", STATUS_OK

    def _read(self, command: bytes):
        if len(command) != 5:
            return b"", STATUS_INVALID_PARAMS
        handle, maxlen = command[2], command[3] | command[4] << 8
        entry = self.handles.get(handle)
        if entry is None:
            return b"\xff\xff", status("02,NO DATA: 9")
        if maxlen == 0 or maxlen > READ_MAX:
            return b"", STATUS_OUT_OF_RANGE
        if entry.sticky is not None:
            return b"\x00\x00", entry.sticky
        entry.sock.settimeout(0.25)
        try:
            data = entry.sock.recv(maxlen)
        except (TimeoutError, ssl.SSLWantReadError):
            return b"\xff\xff", status("02,NO DATA: 11")
        except ssl.SSLEOFError:
            self.handles.pop(handle, None)
            entry.sock.close()
            return b"\x00\x00", status("05,TLS CLOSED WITHOUT NOTIFY")
        except ConnectionResetError:
            entry.sticky = status("12,RECEIVE ERROR: 104")
            return b"\x00\x00", entry.sticky
        except ssl.SSLError as exc:
            alert = self._alert(exc)
            entry.sticky = status(f"14,TLS ALERT RECEIVED: {alert}" if alert
                                  else "16,TLS SESSION FAILED: -0x7100")
            return b"\x00\x00", entry.sticky
        if not data:
            self.handles.pop(handle, None)
            if entry.kind == "tls" and self.flavour != "no-notify-reply":
                try:
                    entry.sock.settimeout(0.5)
                    entry.sock.unwrap()   # the answer to the peer's close_notify
                except (OSError, ValueError, ssl.SSLError):
                    pass
            entry.sock.close()
            return b"\x00\x00", status("01,CONNECTION CLOSED BY HOST")
        return len(data).to_bytes(2, "little") + data, STATUS_OK

    def _write(self, command: bytes):
        if len(command) < 3:
            return b"", STATUS_INVALID_PARAMS
        entry = self.handles.get(command[2])
        if entry is None:
            return b"\xff\xff", status("12,SEND ERROR: 9")
        if entry.sticky is not None:
            return b"\xff\xff", entry.sticky
        data = command[3:]
        try:
            entry.sock.settimeout(5)
            entry.sock.sendall(data)
        except OSError as exc:
            entry.sticky = status(f"12,SEND ERROR: {exc.errno or 32}")
            return b"\xff\xff", entry.sticky
        return len(data).to_bytes(2, "little"), STATUS_OK

    @staticmethod
    def _alert(exc: Exception) -> int | None:
        text = str(exc).upper()
        for name, number in ALERTS.items():
            if f"ALERT_{name}" in text:
                return number
        return None

    # -- TLS -----------------------------------------------------------------

    def _open_tls(self, command: bytes):
        if len(command) < 7:
            return b"", STATUS_INVALID_PARAMS
        port, trust, flags = command[2] | command[3] << 8, command[4], command[5]
        pin = None
        if trust == TRUST_PIN:
            if len(command) < 39:
                return b"", STATUS_INVALID_PARAMS
            pin, tail = command[6:38], command[38:]
        else:
            tail = command[6:]
        parts = tail.split(b"\x00")
        host, name = parts[0], None
        if len(parts) == 2:
            name = parts[1] or None
        elif len(parts) == 3:
            if not parts[1] or parts[2]:
                return b"", STATUS_INVALID_PARAMS
            name = parts[1]
        elif len(parts) > 3:
            return b"", STATUS_INVALID_PARAMS
        if not host or len(host) > 253 or (name is not None and len(name) > 253):
            return b"", STATUS_INVALID_PARAMS
        effective = name if name is not None else (None if is_ipv4_literal(host) else host)
        if trust in (TRUST_BUNDLE, TRUST_CUSTOM) and (effective is None or is_ipv4_literal(effective)):
            return b"", STATUS_INVALID_PARAMS
        if trust > TRUST_PIN or flags & 0xFE or (pin is not None and not any(pin)):
            return b"", STATUS_OUT_OF_RANGE
        if not self.ready & 1:
            return b"", status("90,TLS NOT AVAILABLE")
        if sum(1 for e in self.handles.values() if e.kind == "tls") >= MAX_TLS:
            return b"", status("91,TLS SESSIONS IN USE")
        if trust == TRUST_CUSTOM and self.ca_state != "LOADED":
            return b"", status("97,NO CUSTOM CA")
        if trust == TRUST_PIN and effective is not None and is_ipv4_literal(effective):
            effective = None
        address = self._resolve(host.decode("latin-1"))
        if address is None:
            return b"", status("84,UNRESOLVED HOST")
        handle = self._free_handle()
        if handle is None:
            return b"", status("85,ERROR OPENING SOCKET")
        try:
            raw = socket.create_connection((address, port), timeout=10)
        except OSError as exc:
            return b"", status(f"11,ERROR ON CONNECT: {exc.errno or 116}")
        ctx = self._context(trust, flags)
        started = time.monotonic()
        try:
            raw.settimeout(15)
            sock = ctx.wrap_socket(raw, server_hostname=effective.decode("ascii") if effective else None,
                                   suppress_ragged_eofs=False)
        except ssl.SSLCertVerificationError as exc:
            raw.close()
            return b"", self._verify_status(exc)
        except TimeoutError:
            raw.close()
            return b"", status("15,TLS HANDSHAKE TIMEOUT")
        except ConnectionResetError:
            raw.close()
            return b"", status("13,TLS HANDSHAKE FAILED: PEER RESET: 104")
        except ssl.SSLError as exc:
            raw.close()
            alert = self._alert(exc)
            return b"", status(f"14,TLS ALERT RECEIVED: {alert}" if alert
                               else "13,TLS HANDSHAKE FAILED: -0x7780")
        except OSError as exc:
            raw.close()
            return b"", status(f"13,TLS HANDSHAKE FAILED: SEND ERROR: {exc.errno or 32}")
        handshake_ms = int((time.monotonic() - started) * 1000)
        der = sock.getpeercert(binary_form=True)
        spki = self._spki(der)
        if trust == TRUST_PIN and spki != pin:
            sock.close()
            return b"", status("96,PINNED KEY MISMATCH")
        version = 0x0304 if sock.version() == "TLSv1.3" else 0x0303
        suite = SUITES.get(sock.cipher()[0], 0)
        group = 0 if self.flavour == "group-zero" else 0x001D
        bits = 1 if version == 0x0303 else 0
        reply = (bytes([handle]) + version.to_bytes(2, "little") + suite.to_bytes(2, "little")
                 + group.to_bytes(2, "little") + bytes([bits]))
        self.handles[handle] = Entry("tls", sock, claimed=False, open_reply=reply, trust=trust,
                                     spki=spki, handshake_ms=handshake_ms)
        return reply, STATUS_OK

    def _context(self, trust: int, flags: int) -> ssl.SSLContext:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_3 if flags & 1 else ssl.TLSVersion.TLSv1_2
        ctx.set_ciphers(TLS12_CIPHERS)
        if trust == TRUST_PIN or (trust == TRUST_BUNDLE and self.flavour == "bundle-accepts-all"):
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
        elif trust == TRUST_BUNDLE:
            if self.system_bundle:
                ctx.load_default_certs()
            if self.bundle_cafile:
                ctx.load_verify_locations(cafile=self.bundle_cafile)
        else:
            ctx.load_verify_locations(cadata=self.ca_der)
        return ctx

    @staticmethod
    def _spki(der: bytes) -> bytes:
        if x509 is None or not der:
            return b"\x00" * 32
        cert = x509.load_der_x509_certificate(der)
        return hashlib.sha256(cert.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)).digest()

    @staticmethod
    def _verify_status(exc: ssl.SSLCertVerificationError) -> bytes:
        code = exc.verify_code
        if code == 62:
            return status("94,CERTIFICATE NAME MISMATCH: 0x00000004")
        if code == 10:
            return status("95,CERTIFICATE EXPIRED: 0x00000001")
        if code == 9:
            return status("95,CERTIFICATE EXPIRED: 0x00000200")
        if code in (2, 19, 20, 21):
            return status("93,CERTIFICATE NOT TRUSTED: 0x00000008")
        return status("93,CERTIFICATE NOT TRUSTED: 0xFFFFFFFF")

    def _trust_ca(self, command: bytes):
        if len(command) < 6:
            return b"", STATUS_INVALID_PARAMS
        offset, total = command[2] | command[3] << 8, command[4] | command[5] << 8
        chunk = command[6:]
        if offset == 0:
            if total == 0:
                if chunk:
                    return b"", STATUS_INVALID_PARAMS
                self.ca_state, self.ca_der = "EMPTY", None
                return b"", STATUS_OK
            if total > CA_MAX:
                self.ca_state, self.ca_der = "EMPTY", None
                return b"", STATUS_OUT_OF_RANGE
            self.ca_state, self.ca_der = "UPLOADING", None
            self.ca_buf, self.ca_total = bytearray(), total
        elif self.ca_state != "UPLOADING" or offset != len(self.ca_buf) or total != self.ca_total:
            if self.ca_state == "UPLOADING":
                self.ca_state = "EMPTY"
            return b"", STATUS_INVALID_PARAMS
        if len(self.ca_buf) + len(chunk) > self.ca_total:
            self.ca_state = "EMPTY"
            return b"", STATUS_INVALID_PARAMS
        self.ca_buf += chunk
        if len(self.ca_buf) < self.ca_total:
            return b"", STATUS_OK
        der = bytes(self.ca_buf)
        try:
            cert = x509.load_der_x509_certificate(der)
            constraints = cert.extensions.get_extension_for_class(x509.BasicConstraints).value
            if not constraints.ca:
                self.ca_state = "EMPTY"
                return b"", status("97,CUSTOM CA REJECTED: NOT A CA")
        except Exception:
            self.ca_state = "EMPTY"
            return b"", status("97,CUSTOM CA INVALID: -0x2180")
        self.ca_state, self.ca_der = "LOADED", der
        return b"", STATUS_OK

    def _info(self, command: bytes):
        if len(command) != 3:
            return b"", STATUS_INVALID_PARAMS
        handle = command[2]
        tls = [e for e in self.handles.values() if e.kind == "tls"]
        if handle == 0xFF:
            ready = self.ready | (0x08 if self.ca_state == "LOADED" else 0)
            record = bytearray(16)
            record[0:6] = bytes([1, MAX_TLS, len(tls), 0x07, ready, 0x03])
            record[6:9] = bytes([1]) + (8).to_bytes(2, "little")
            record[9:12] = b"\xff\xff\xff" if ready & 1 else b"\x00\x00\x00"
            record[12:14] = (3).to_bytes(2, "little")
            record[14] = sum(1 for e in tls if not e.claimed)
            return bytes(record), STATUS_OK
        entry = self.handles.get(handle)
        if entry is None or entry.kind != "tls":
            return b"", STATUS_INVALID_PARAMS
        r = entry.open_reply
        record = (r[1:7] + bytes([entry.trust, r[7]]) + entry.spki
                  + entry.handshake_ms.to_bytes(4, "little") + bytes(4))
        return record, STATUS_OK

    def _release(self, command: bytes):
        if len(command) != 2:
            return b"", STATUS_INVALID_PARAMS
        closed = 0
        if self.flavour != "release-noop":
            for handle, entry in list(self.handles.items()):
                if entry.kind == "tls" and not entry.claimed:
                    self._close(bytes([3, NET_CMD_CLOSE_SOCKET, handle]))
                    closed += 1
        return bytes([closed]), STATUS_OK
