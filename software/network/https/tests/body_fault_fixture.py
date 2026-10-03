"""Authenticated local response interruption followed by recovery on one listener."""
import socket
import ssl
import struct
import threading
import time
from contextlib import contextmanager


@contextmanager
def body_fault_server(cert, key, mode):
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen()
    listener.settimeout(0.1)
    stopped, events, errors = threading.Event(), [], []

    def serve():
        try:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(cert, key)
            for attempt in range(2):
                while not stopped.is_set():
                    try:
                        connection, _ = listener.accept()
                        break
                    except TimeoutError:
                        continue
                else:
                    return
                with connection:
                    connection.settimeout(3)
                    with context.wrap_socket(connection, server_side=True) as channel:
                        request = b''
                        while b'\r\n\r\n' not in request:
                            part = channel.recv(512)
                            if not part:
                                raise ValueError('No complete authenticated HTTP request')
                            request += part
                            assert len(request) <= 1024
                        assert request == b'GET /data HTTP/1.1\r\nHost: localhost\r\n\r\n'
                        event = {'attempt': attempt, 'tls_version': channel.version(), 'request_received': True,
                                 'declared_bytes': 2048 if attempt == 0 else 7,
                                 'sent_body_bytes': 1024 if attempt == 0 else 7}
                        body = b'x'*1024 if attempt == 0 else b'{"x":1}'
                        channel.sendall(f"HTTP/1.1 200 OK\r\nContent-Length: {event['declared_bytes']}\r\n\r\n".encode()+body)
                        if attempt == 0:
                            # Let the client consume the partial TLS record before interruption.
                            time.sleep(0.2)
                            if mode == 'body-stall':
                                stopped.wait(2.1)
                            elif mode == 'body-reset':
                                channel.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
                            elif mode == 'body-clean':
                                try:
                                    channel.unwrap().close()
                                except ssl.SSLEOFError:
                                    # The client rejects the short body and closes TCP without
                                    # returning close_notify. Its probe must independently
                                    # confirm that our close_notify arrived (TLS status OK).
                                    pass
                            elif mode != 'body-eof':
                                raise ValueError('Unknown body fault')
                            event['action'] = mode
                        else:
                            event['action'] = 'recovery'
                        events.append(event)
                        # close() without unwrap() intentionally omits close_notify.
        except Exception as error:  # noqa: BLE001 - propagate fixture thread failures to the test
            errors.append(error)

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        yield listener.getsockname()[1], events
    finally:
        stopped.set()
        thread.join(5)
        listener.close()
        assert not thread.is_alive() and not errors, errors
