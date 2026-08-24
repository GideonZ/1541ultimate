"""Wi-Fi controlled keyboard fixture.  Configuration is deployed as config.py."""
import json
import machine
import network
import select
import socket
import time
from config import WIFI_PASSWORD, WIFI_SSID
from u64_hid_keyboard import keyboard

PROTOCOL_VERSION = 1
MAGIC = "u64-usb-keyboard-soak"
DISCOVERY_PORT = 49196
TCP_PORT = 49197
MAX_LINE = 512
KEYS = {"up": 0x52, "down": 0x51, "left": 0x50, "right": 0x4F,
        "a": 0x04, "b": 0x05, "f13": 0x68}


class Service:
    def __init__(self):
        if keyboard is None:
            raise RuntimeError("USB keyboard was not configured by boot.py")
        self.kbd = keyboard
        self.device_id = "".join("%02x" % x for x in machine.unique_id())
        self.commands_handled = 0
        self.fault_mode = "none"
        self.held = ""

    def status(self):
        return {"service": MAGIC, "protocol_version": PROTOCOL_VERSION,
                "device_id": self.device_id, "ip": network.WLAN(network.STA_IF).ifconfig()[0],
                "idle_rate": self.kbd.idle_rate, "hid_open": self.kbd.is_open(),
                "reports_sent": self.kbd.reports_sent, "commands_handled": self.commands_handled,
                "fault_mode": self.fault_mode, "currently_held_keys": [self.held] if self.held else []}

    def send(self):
        self.kbd.send_now()

    def release_all(self):
        self.fault_mode = "none"
        self.held = ""
        self.kbd.set_key(0)
        self.send()

    def wait(self, milliseconds, silence=False):
        deadline = time.ticks_add(time.ticks_ms(), milliseconds)
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            self.kbd.service_idle(silence)
            time.sleep_ms(2)

    def execute(self, request):
        if request.get("protocol_version") != PROTOCOL_VERSION:
            raise ValueError("protocol version mismatch")
        command = request.get("command")
        if command == "status":
            return self.status()
        key = request.get("key", "down")
        if key not in KEYS:
            raise ValueError("unsupported key")
        duration = request.get("duration_ms", 30)
        if not isinstance(duration, int) or duration < 10 or duration > 2000:
            raise ValueError("duration_ms outside 10..2000")
        if command == "release_all":
            self.release_all()
        elif command == "tap":
            self.held = key; self.kbd.set_key(KEYS[key]); self.send(); self.wait(duration)
            self.release_all()
        elif command == "hold":
            self.held = key; self.kbd.set_key(KEYS[key]); self.send(); self.wait(duration)
            self.release_all()
        elif command in ("drop_release_once", "silence_after_press"):
            fault_duration = request.get("fault_duration_ms", 750)
            if not isinstance(fault_duration, int) or fault_duration < 400 or fault_duration > 1200:
                raise ValueError("fault_duration_ms outside 400..1200")
            self.held = key; self.kbd.set_key(KEYS[key]); self.send(); self.wait(duration)
            self.held = ""; self.kbd.set_key(0)
            self.fault_mode = command
            # drop-release keeps idle reports alive; silence suppresses every
            # report, proving U64's independently implemented stale timeout.
            self.wait(fault_duration, command == "silence_after_press")
            self.release_all()
        else:
            raise ValueError("unknown command")
        self.commands_handled += 1
        return self.status()


def connect_wifi():
    wlan = network.WLAN(network.STA_IF); wlan.active(True)
    wlan.connect(WIFI_SSID, WIFI_PASSWORD)
    deadline = time.ticks_add(time.ticks_ms(), 30000)
    while not wlan.isconnected() and time.ticks_diff(deadline, time.ticks_ms()) > 0:
        time.sleep_ms(200)
    if not wlan.isconnected():
        raise RuntimeError("Wi-Fi association failed")


def reply(conn, request, service):
    request_id = request.get("id")
    if not isinstance(request_id, int):
        raise ValueError("missing numeric request id")
    try:
        result = service.execute(request)
        response = {"protocol_version": PROTOCOL_VERSION, "id": request_id, "ok": True, "result": result}
    except Exception as exc:
        response = {"protocol_version": PROTOCOL_VERSION, "id": request_id, "ok": False, "error": str(exc)}
    conn.send((json.dumps(response) + "\n").encode())


def main():
    connect_wifi()
    service = Service()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp.bind(("", DISCOVERY_PORT)); udp.setblocking(False)
    tcp = socket.socket(); tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); tcp.bind(("", TCP_PORT)); tcp.listen(1); tcp.setblocking(False)
    # Polling the CYW43 UDP socket is unreliable on some Pico W firmware
    # revisions.  It is non-blocking, so drain discovery datagrams directly
    # and reserve poll() for the TCP listener and clients.
    poll = select.poll(); poll.register(tcp, select.POLLIN)
    clients = {}
    while True:
        service.kbd.service_idle(service.fault_mode == "silence_after_press")
        try:
            data, address = udp.recvfrom(MAX_LINE)
            if json.loads(data).get("service") == MAGIC:
                udp.sendto(json.dumps(service.status()).encode(), address)
        except OSError:
            pass
        except Exception:
            pass
        for obj, event in poll.poll(20):
            if obj == tcp:
                conn, _ = tcp.accept(); conn.setblocking(False); clients[conn] = b""; poll.register(conn, select.POLLIN)
            else:
                try:
                    chunk = obj.recv(MAX_LINE)
                    if not chunk: raise OSError()
                    clients[obj] += chunk
                    if len(clients[obj]) > MAX_LINE: raise ValueError("request too long")
                    while b"\n" in clients[obj]:
                        line, clients[obj] = clients[obj].split(b"\n", 1)
                        try: reply(obj, json.loads(line), service)
                        except Exception as exc: obj.send((json.dumps({"protocol_version": PROTOCOL_VERSION, "id": None, "ok": False, "error": str(exc)}) + "\n").encode())
                except Exception:
                    poll.unregister(obj); clients.pop(obj, None); obj.close()


main()
