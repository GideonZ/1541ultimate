#!/usr/bin/env python3
"""The host side of the UCI TLS suite: a throwaway PKI and TLS servers that log what they saw.

The servers run on this machine. Everything the device claims is
held against what the server logged for the same connection: the version and
suite the server negotiated, the SNI it received, the sha256 of the plaintext
it received, and whether the client ended with close_notify, a bare FIN or a
reset. The device's own claim is never the evidence for itself.

The PKI is generated per run in a temporary directory, is never committed and
is never printed (only SPKI hashes are). It needs the `cryptography` package,
which the scenarios that need a local server report as a SKIP when it is
missing, rather than failing for a reason that is not the firmware's.

    root          P-256, the CA a CUSTOM upload installs
    intermediate  P-256, served with every leaf
    leaf ecdsa    P-256, FIXTURE_NAME
    leaf rsa      RSA-2048, FIXTURE_NAME
    leaf wrong    P-256, WRONG_NAME only (94)
    leaf expired  P-256, FIXTURE_NAME, notAfter a year ago (95)
    leaf future   P-256, FIXTURE_NAME, notBefore a year ahead (95)
    leaf unknown  P-256, FIXTURE_NAME, under a root nobody trusts (93)

A server is one listening socket and a thread per connection, with one
`Behaviour`. The device reaches it at this machine's address on the route to
the device, as net_target_test.py's Peer does.
"""

import datetime
import hashlib
import socket
import ssl
import struct
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

try:
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, rsa
    from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
    HAVE_CRYPTOGRAPHY = True
except ImportError:  # pragma: no cover - reported as a SKIP by the suite
    HAVE_CRYPTOGRAPHY = False

FIXTURE_NAME = "fixture.test"
WRONG_NAME = "wrong-host.test"
ACCEPT_TIMEOUT = 60.0
# Idle limit per connection: it must outlast the slowest route (over REST a device
# took about 250 s to read S13's 16 KB record), yet still bound every wait;
# TlsServer.close() ends whatever is still open.
CONN_TIMEOUT = 600.0
# How long the server waits for the client's answer to its close_notify.
NOTIFY_REPLY_WAIT = 120.0


def payload(seed: int, length: int) -> bytes:
    """A seeded payload neither side can produce by accident: SHA-256 in counter mode."""
    out = bytearray()
    counter = 0
    while len(out) < length:
        out += hashlib.sha256(struct.pack("<II", seed, counter)).digest()
        counter += 1
    return bytes(out[:length])


def spki_sha256(cert_der: bytes) -> bytes:
    """SHA-256 of a certificate's SubjectPublicKeyInfo, digest order."""
    cert = x509.load_der_x509_certificate(cert_der)
    spki = cert.public_key().public_bytes(serialization.Encoding.DER,
                                          serialization.PublicFormat.SubjectPublicKeyInfo)
    return hashlib.sha256(spki).digest()


@dataclass
class Chain:
    """One server identity: files for ssl.load_cert_chain, and what the device should see."""
    name: str
    cert_file: Path      # leaf then intermediate, PEM
    key_file: Path
    leaf_der: bytes
    spki_sha256: bytes


class Pki:
    """The per-run test PKI. Private keys live only in `directory`."""

    def __init__(self, directory: Path | None = None) -> None:
        if not HAVE_CRYPTOGRAPHY:
            raise RuntimeError("the `cryptography` package is not installed "
                               "(pip install cryptography); the local TLS fixture needs it")
        self._tmp = None
        if directory is None:
            self._tmp = tempfile.TemporaryDirectory(prefix="net-tls-pki-")
            directory = Path(self._tmp.name)
        self.dir = directory
        now = datetime.datetime.now(datetime.UTC)
        self.now = now
        self.root_key = ec.generate_private_key(ec.SECP256R1())
        self.root = self._ca("E2E Test Root", self.root_key, None, None, now)
        self.int_key = ec.generate_private_key(ec.SECP256R1())
        self.intermediate = self._ca("E2E Test Intermediate", self.int_key,
                                     self.root, self.root_key, now)
        self.unknown_key = ec.generate_private_key(ec.SECP256R1())
        self.unknown_root = self._ca("E2E Unknown Root", self.unknown_key, None, None, now)
        self.chains: dict[str, Chain] = {}
        day = datetime.timedelta(days=1)
        self._leaf("ecdsa", FIXTURE_NAME, ec.generate_private_key(ec.SECP256R1()),
                   now - day, now + 30 * day)
        self._leaf("rsa", FIXTURE_NAME, rsa.generate_private_key(65537, 2048),
                   now - day, now + 30 * day)
        self._leaf("wrong", WRONG_NAME, ec.generate_private_key(ec.SECP256R1()),
                   now - day, now + 30 * day)
        self._leaf("expired", FIXTURE_NAME, ec.generate_private_key(ec.SECP256R1()),
                   now - 400 * day, now - 365 * day)
        self._leaf("future", FIXTURE_NAME, ec.generate_private_key(ec.SECP256R1()),
                   now + 365 * day, now + 400 * day)
        self._leaf("unknown", FIXTURE_NAME, ec.generate_private_key(ec.SECP256R1()),
                   now - day, now + 30 * day, issuer=(self.unknown_root, self.unknown_key),
                   with_intermediate=False)

    def close(self) -> None:
        if self._tmp is not None:
            self._tmp.cleanup()

    @property
    def root_der(self) -> bytes:
        return self.root.public_bytes(serialization.Encoding.DER)

    def root_pem_file(self) -> Path:
        path = self.dir / "root.pem"
        path.write_bytes(self.root.public_bytes(serialization.Encoding.PEM))
        return path

    @staticmethod
    def _name(cn: str) -> x509.Name:
        return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])

    def _ca(self, cn, key, issuer, issuer_key, now) -> "x509.Certificate":
        day = datetime.timedelta(days=1)
        builder = (x509.CertificateBuilder()
                   .subject_name(self._name(cn))
                   .issuer_name(issuer.subject if issuer is not None else self._name(cn))
                   .public_key(key.public_key())
                   .serial_number(x509.random_serial_number())
                   .not_valid_before(now - 2 * day)
                   .not_valid_after(now + 60 * day)
                   .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
                   .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()),
                                  critical=False)
                   .add_extension(x509.KeyUsage(digital_signature=False, content_commitment=False,
                                                key_encipherment=False, data_encipherment=False,
                                                key_agreement=False, key_cert_sign=True,
                                                crl_sign=True, encipher_only=False,
                                                decipher_only=False), critical=True))
        signer = issuer_key if issuer_key is not None else key
        builder = builder.add_extension(
            x509.AuthorityKeyIdentifier.from_issuer_public_key(signer.public_key()), critical=False)
        return builder.sign(signer, hashes.SHA256())

    def _leaf(self, label, cn, key, not_before, not_after, issuer=None,
              with_intermediate=True) -> None:
        ca, ca_key = issuer if issuer is not None else (self.intermediate, self.int_key)
        cert = (x509.CertificateBuilder()
                .subject_name(self._name(cn))
                .issuer_name(ca.subject)
                .public_key(key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(not_before)
                .not_valid_after(not_after)
                .add_extension(x509.SubjectAlternativeName([x509.DNSName(cn)]), critical=False)
                .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),
                               critical=False)
                .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()),
                               critical=False)
                .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(
                    ca_key.public_key()), critical=False)
                .sign(ca_key, hashes.SHA256()))
        cert_file = self.dir / f"{label}.pem"
        key_file = self.dir / f"{label}.key"
        pem = cert.public_bytes(serialization.Encoding.PEM)
        if with_intermediate:
            pem += self.intermediate.public_bytes(serialization.Encoding.PEM)
        cert_file.write_bytes(pem)
        key_file.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                               serialization.PrivateFormat.PKCS8,
                                               serialization.NoEncryption()))
        der = cert.public_bytes(serialization.Encoding.DER)
        self.chains[label] = Chain(label, cert_file, key_file, der, spki_sha256(der))

    def issue(self, label: str, cn: str) -> Chain:
        """One more P-256 leaf under the test root, for a stand-in host (self-test)."""
        day = datetime.timedelta(days=1)
        self._leaf(label, cn, ec.generate_private_key(ec.SECP256R1()),
                   self.now - day, self.now + 30 * day)
        return self.chains[label]


@dataclass
class Behaviour:
    """What one server does with each connection."""
    min_version: "ssl.TLSVersion" = ssl.TLSVersion.TLSv1_3
    max_version: "ssl.TLSVersion" = ssl.TLSVersion.TLSv1_3
    # OpenSSL cipher string for TLS 1.2; TLS 1.3 suites are left at their default.
    ciphers: str | None = None
    # One curve name ("prime256v1"), to force an HRR or a P-256 group.
    ecdh_curve: str | None = None
    num_tickets: int | None = None
    # Seconds to hold the ClientHello before answering it.
    delay_handshake: float = 0.0
    # Accept the TCP connection and never answer (S10).
    stall: bool = False
    # Read the ClientHello, then reset the connection (S7c).
    rst_after_client_hello: bool = False
    # The listener's SO_RCVBUF, so a server that stops reading fills TCP soon.
    rcvbuf: int | None = None
    # Sent once, as one write, right after the handshake.
    send: bytes = b""
    # After `send`: "echo" (until the client ends), "close_notify", "fin" (no
    # close_notify), "hold" (read and discard), "http" (one 200, then
    # close_notify), or "stall" (never read again, so writes fill TCP; S32).
    then: str = "echo"


@dataclass
class ConnLog:
    """What the server saw on one connection."""
    peer: str
    sni: str | None = None
    version: str | None = None
    cipher: str | None = None
    received: bytearray = field(default_factory=bytearray)
    sent: int = 0
    # How the client ended it: "close_notify", "fin", "reset", "open" (still
    # open when the server gave up), or "handshake" (it never completed).
    peer_end: str = "open"
    # After the server's own close_notify ("close_notify" mode): whether the
    # client answered with one before it closed (True), or ended without it
    # (False). None when the server did not close that way.
    notify_reply: bool | None = None
    error: str | None = None
    done: threading.Event = field(default_factory=threading.Event)

    def as_dict(self) -> dict:
        return {"peer": self.peer, "sni": self.sni, "version": self.version,
                "cipher": self.cipher, "received": len(self.received),
                "notify_reply": self.notify_reply,
                "received_sha256": hashlib.sha256(self.received).hexdigest(),
                "sent": self.sent, "peer_end": self.peer_end, "error": self.error}


class TlsServer:
    """One TLS server on this machine, with one Behaviour for every connection."""

    def __init__(self, bind_ip: str, chain: Chain | None, behaviour: Behaviour,
                 port: int = 0) -> None:
        self.behaviour = behaviour
        self.chain = chain
        self.logs: list[ConnLog] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if behaviour.rcvbuf:
            self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, behaviour.rcvbuf)
        self.listener.bind((bind_ip, port))
        self.listener.listen(8)
        self.listener.settimeout(0.2)
        self.ip, self.port = self.listener.getsockname()[:2]
        self.context = self._context() if chain is not None else None
        self._threads: list[threading.Thread] = []
        # Every accepted socket, so close() can end the ones still waiting.
        self._open: set = set()
        self._acceptor = threading.Thread(target=self._accept_loop, daemon=True)
        self._acceptor.start()

    def _context(self) -> ssl.SSLContext:
        b = self.behaviour
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = b.min_version
        ctx.maximum_version = b.max_version
        ctx.load_cert_chain(str(self.chain.cert_file), str(self.chain.key_file))
        if b.ciphers:
            ctx.set_ciphers(b.ciphers)
        if b.ecdh_curve:
            ctx.set_ecdh_curve(b.ecdh_curve)
        if b.num_tickets is not None:
            ctx.num_tickets = b.num_tickets

        def sni(sslobj, name, _ctx) -> None:
            log = getattr(sslobj, "_net_tls_log", None)
            if log is not None:
                log.sni = name
        ctx.sni_callback = sni
        return ctx

    def _accept_loop(self) -> None:
        while not self._stop.is_set():
            try:
                raw, addr = self.listener.accept()
            except TimeoutError:
                continue
            except OSError:
                return
            log = ConnLog(f"{addr[0]}:{addr[1]}")
            with self._lock:
                self.logs.append(log)
            thread = threading.Thread(target=self._serve, args=(raw, log), daemon=True)
            self._threads.append(thread)
            thread.start()

    def _serve(self, raw: socket.socket, log: ConnLog) -> None:
        b = self.behaviour
        # wrap_socket detaches `raw`, so whichever object owns the descriptor
        # is the one to read from and to close.
        conn = raw
        with self._lock:
            self._open.add(raw)
        try:
            raw.settimeout(CONN_TIMEOUT)
            if b.stall:
                log.peer_end = self._hold_raw(raw)
                return
            if b.rst_after_client_hello:
                raw.recv(4096)
                raw.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                log.peer_end = "handshake"
                log.error = "reset after the ClientHello (by design)"
                return
            if b.delay_handshake:
                time.sleep(b.delay_handshake)
            tls = self.context.wrap_socket(raw, server_side=True,
                                           do_handshake_on_connect=False,
                                           suppress_ragged_eofs=False)
            conn = tls
            with self._lock:
                self._open.discard(raw)
                self._open.add(tls)
            tls._net_tls_log = log  # read by the SNI callback
            try:
                tls.do_handshake()
            except (ssl.SSLError, OSError) as exc:
                log.peer_end = "handshake"
                log.error = f"handshake: {exc}"
                return
            log.version = tls.version()
            log.cipher = tls.cipher()[0] if tls.cipher() else None
            if b.send:
                tls.sendall(b.send)
                log.sent += len(b.send)
            if b.then in ("close_notify", "fin"):
                # Let the device's reads be what ends the stream.
                time.sleep(0.5)
                if b.then == "close_notify":
                    # SSL_shutdown sends ours and waits for the peer's. It returns only when the client's
                    # close_notify arrived; a bare FIN or reset raises instead.
                    tls.settimeout(NOTIFY_REPLY_WAIT)
                    try:
                        tls.unwrap()
                        log.notify_reply = True
                    except (ssl.SSLError, OSError):
                        log.notify_reply = False
                # SSLSocket.shutdown drops the TLS layer first, so after
                # "fin" this is a bare FIN with no close_notify ahead of it.
                tls.shutdown(socket.SHUT_WR)
                log.peer_end = self._drain_after_close(tls)
                return
            if b.then == "stall":
                while not self._stop.wait(0.2):
                    pass
                return
            if b.then == "http":
                self._http(tls, log)
                return
            self._echo(tls, log, echo=(b.then == "echo"))
        except Exception as exc:  # the log is the report; the thread must not die silently
            log.error = f"{type(exc).__name__}: {exc}"
        finally:
            with self._lock:
                self._open.discard(conn)
                self._open.discard(raw)
            try:
                conn.close()
            except OSError:
                pass
            log.done.set()

    def _echo(self, tls: ssl.SSLSocket, log: ConnLog, echo: bool) -> None:
        while not self._stop.is_set():
            try:
                data = tls.recv(4096)
            except ssl.SSLEOFError:
                log.peer_end = "fin"
                return
            except ssl.SSLZeroReturnError:
                log.peer_end = "close_notify"
                return
            except (ConnectionResetError, BrokenPipeError):
                log.peer_end = "reset"
                return
            except TimeoutError:
                log.peer_end = "open"
                return
            except ssl.SSLError as exc:
                log.peer_end = "error"
                log.error = f"recv: {exc}"
                return
            if not data:
                log.peer_end = "close_notify"
                return
            log.received += data
            if echo:
                tls.sendall(data)
                log.sent += len(data)

    def _http(self, tls: ssl.SSLSocket, log: ConnLog) -> None:
        while b"\r\n\r\n" not in log.received:
            data = tls.recv(4096)
            if not data:
                log.peer_end = "close_notify"
                return
            log.received += data
        body = b"ok\n"
        response = (b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
                    % len(body)) + body
        tls.sendall(response)
        log.sent += len(response)
        try:
            tls.unwrap()
        except (ssl.SSLError, OSError):
            pass
        log.peer_end = "close_notify"

    @staticmethod
    def _hold_raw(raw: socket.socket) -> str:
        try:
            while raw.recv(4096):
                pass
            return "fin"
        except (ConnectionResetError, BrokenPipeError):
            return "reset"
        except TimeoutError:
            return "open"

    @staticmethod
    def _drain_after_close(raw: socket.socket) -> str:
        """How the client ended a connection the server has already closed for writing."""
        # The same idle bound: after the server's own close, a REST-route client
        # can take minutes to read what is left (S6/S7: about 45 s for 3000 B).
        raw.settimeout(CONN_TIMEOUT)
        try:
            while raw.recv(4096):
                pass
            return "fin"
        except (ConnectionResetError, BrokenPipeError):
            return "reset"
        except TimeoutError:
            return "open"
        except OSError:
            return "closed"

    def wait_connections(self, count: int, timeout: float = 15.0) -> list[ConnLog]:
        """The first `count` connections, once each has finished (or `timeout`)."""
        deadline = time.monotonic() + timeout
        while True:
            with self._lock:
                logs = list(self.logs)
            if len(logs) >= count and all(log.done.is_set() for log in logs[:count]):
                return logs[:count]
            if time.monotonic() > deadline:
                return logs[:count]
            time.sleep(0.05)

    def close(self) -> None:
        self._stop.set()
        try:
            self.listener.close()
        except OSError:
            pass
        with self._lock:
            still_open = list(self._open)
        for sock in still_open:
            # Wakes a thread blocked in recv or unwrap, so it ends now rather
            # than at CONN_TIMEOUT.
            try:
                # The plain socket's shutdown, under any TLS wrapper the other
                # thread is still reading through.
                socket.socket.shutdown(sock, socket.SHUT_RDWR)
            except OSError:
                pass
        self._acceptor.join(timeout=2)


def local_address_towards(device_host: str) -> str:
    """This machine's address on the route to the device (as net_target_test.py)."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((device_host, 9))
        return probe.getsockname()[0]
    finally:
        probe.close()
