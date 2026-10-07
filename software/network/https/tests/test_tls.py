"""Local TLS tests of tls_stream.c against a real TLS server; no device access."""
import argparse
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID


def run(*args):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(f"{args[0]} failed\n{result.stdout}\n{result.stderr}")
    return result.stdout


def certificate(folder, name, expired=False, future=False):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now + timedelta(days=1) if future else now - timedelta(days=3))
            .not_valid_after(now - timedelta(days=1) if expired else now + timedelta(days=3))
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost")]), critical=False)
            .sign(key, hashes.SHA256()))
    cert_path, key_path = folder / f"{name}.pem", folder / f"{name}.key"
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                        serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    return cert_path, key_path


@contextmanager
def server(cert, key, behavior="normal", tls12=False):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen()
    listener.settimeout(0.1)
    stopped, errors = threading.Event(), []

    def serve():
        try:
            while not stopped.is_set():
                try:
                    connection, _ = listener.accept()
                    break
                except TimeoutError:
                    continue
            else:
                return
            with connection:
                connection.settimeout(2)
                if behavior == "stall":
                    stopped.wait(2)
                    return
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = ssl.TLSVersion.TLSv1_2
                if tls12:
                    context.maximum_version = ssl.TLSVersion.TLSv1_2
                context.load_cert_chain(cert, key)
                with context.wrap_socket(connection, server_side=True) as channel:
                    request = b""
                    while b"\r\n\r\n" not in request:
                        part = channel.recv(512)
                        if not part:
                            return
                        request += part
                        assert len(request) <= 1024
                    assert request == b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
                    if behavior == "read-stall":
                        stopped.wait(2)
                        return
                    channel.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na\0b")
                    if behavior == "abrupt":
                        return  # No TLS close_notify.
                    channel.unwrap().close()
        except (ssl.SSLError, ConnectionError, TimeoutError):
            pass  # Peer rejection/close is expected in negative cases.
        except Exception as error:
            errors.append(error)

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        yield listener.getsockname()[1]
    finally:
        stopped.set()
        thread.join(4)
        listener.close()
        assert not thread.is_alive()
        assert not errors, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mbedtls-source", type=Path, required=True)
    parser.add_argument("--mbedtls-build", type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="ultimate-tls-") as tmp:
        folder = Path(tmp)
        probe = folder / "probe"
        run("cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", here.parent, "-I", args.mbedtls_source / "include",
            here / "tls_probe.c", here.parent / "tls_stream.c", here.parent / "https_client.c",
            args.mbedtls_build / "library/libmbedtls.a",
            args.mbedtls_build / "library/libmbedx509.a",
            args.mbedtls_build / "library/libmbedcrypto.a", "-o", probe)
        trusted = certificate(folder, "trusted")
        other = certificate(folder, "other")
        expired = certificate(folder, "expired", expired=True)
        future = certificate(folder, "future", future=True)
        # Expected status values from https_client.h, not merely process failure.
        cases = [
            ("valid TLS", trusted, trusted[0], "localhost", 0, 512, 0, "normal", False),
            ("TLS 1.2 short I/O", trusted, trusted[0], "localhost", 0, 7, 0, "normal", True),
            ("wrong hostname", trusted, trusted[0], "wrong.test", 0, 512, 5, "normal", False),
            ("untrusted", other, trusted[0], "localhost", 0, 512, 5, "normal", False),
            ("expired", expired, expired[0], "localhost", 0, 512, 5, "normal", False),
            ("not yet valid", future, future[0], "localhost", 0, 512, 5, "normal", False),
            ("missing/stale time", trusted, trusted[0], "localhost", 1, 512, 6, "normal", False),
            ("cancel before open", trusted, trusted[0], "localhost", 2, 512, 2, "normal", False),
            ("cancel during handshake", trusted, trusted[0], "localhost", 3, 512, 2, "stall", False),
            ("TCP open failure", trusted, trusted[0], "localhost", 4, 512, 4, "normal", False),
            ("trust store failure", trusted, trusted[0], "localhost", 5, 512, 5, "normal", False),
            ("invalid read count", trusted, trusted[0], "localhost", 6, 512, 4, "normal", False),
            ("zero write", trusted, trusted[0], "localhost", 7, 512, 4, "normal", False),
            ("handshake deadline", trusted, trusted[0], "localhost", 0, 512, 3, "stall", False),
            ("read deadline", trusted, trusted[0], "localhost", 0, 512, 3, "read-stall", False),
            ("abrupt EOF", trusted, trusted[0], "localhost", 0, 512, 8, "abrupt", False),
        ]
        stages = {"missing/stale time": 1, "cancel before open": 0,
                  "TCP open failure": 7, "trust store failure": 4}
        read_cases = {"valid TLS", "TLS 1.2 short I/O", "read deadline", "abrupt EOF"}
        certificate_cases = {"wrong hostname", "untrusted", "expired", "not yet valid"}
        for name, cert, roots, host, mode, fragment, expected, behavior, tls12 in cases:
            with server(*cert, behavior=behavior, tls12=tls12) as port:
                stage = stages.get(name, 9 if name in read_cases else 8)
                run(probe, roots, port, host, mode, fragment, expected, 1000,
                    stage, int(name in certificate_cases))
            print(f"PASS {name}", flush=True)
        print(f"{len(cases)} TLS cases passed (ASan/UBSan enabled for adapter and probe).")


if __name__ == "__main__":
    main()
