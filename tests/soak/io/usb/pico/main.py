"""Wi-Fi controlled keyboard and mouse fixture.  Configuration is deployed as config.py."""
import json
import machine
import network
import select
import socket
import time
from config import WIFI_PASSWORD, WIFI_SSID
from u64_hid_keyboard import keyboard, mouse

PROTOCOL_VERSION = 1
MAGIC = "u64-usb-keyboard-soak"
DISCOVERY_PORT = 49196
TCP_PORT = 49197
MAX_LINE = 512
# A fixed table of HID keyboard usages.  The fixture can only send these.
KEYS = {"up": 0x52, "down": 0x51, "left": 0x50, "right": 0x4F,
        "return": 0x28, "escape": 0x29, "backspace": 0x2A, "tab": 0x2B, "space": 0x2C,
        "home": 0x4A, "delete": 0x4C, "f13": 0x68}
for _index, _letter in enumerate("abcdefghijklmnopqrstuvwxyz"):
    KEYS[_letter] = 0x04 + _index
for _index, _digit in enumerate("1234567890"):
    KEYS[_digit] = 0x1E + _index
for _number in range(1, 13):
    KEYS["f%d" % _number] = 0x39 + _number


class Service:
    def __init__(self):
        if keyboard is None:
            raise RuntimeError("USB keyboard was not configured by boot.py")
        self.kbd = keyboard
        self.mouse = mouse
        self.device_id = "".join("%02x" % x for x in machine.unique_id())
        self.commands_handled = 0
        self.fault_mode = "none"
        self.held = ""
        self.last_stream = {"sent": 0, "elapsed_ms": 0}

    def status(self):
        return {"service": MAGIC, "protocol_version": PROTOCOL_VERSION,
                "device_id": self.device_id, "ip": network.WLAN(network.STA_IF).ifconfig()[0],
                "idle_rate": self.kbd.idle_rate, "hid_open": self.kbd.is_open(),
                "reports_sent": self.kbd.reports_sent, "commands_handled": self.commands_handled,
                "fault_mode": self.fault_mode, "currently_held_keys": [self.held] if self.held else [],
                "capabilities": ["keyboard", "mouse"] if self.mouse else ["keyboard"],
                "mouse_open": bool(self.mouse and self.mouse.is_open()),
                "mouse_reports_sent": self.mouse.reports_sent if self.mouse else 0,
                "last_stream": self.last_stream}

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
        if isinstance(command, str) and command.startswith("mouse_"):
            self.execute_mouse(command, request)
            self.commands_handled += 1
            return self.status()
        key = request.get("key", "down")
        if key not in KEYS:
            raise ValueError("unsupported key")
        duration = request.get("duration_ms", 30)
        if not isinstance(duration, int) or duration < 10 or duration > 2000:
            raise ValueError("duration_ms outside 10..2000")
        if command == "release_all":
            self.release_all()
        elif command == "press":
            # Holds the key until release_all; idle reports keep it fresh.
            self.held = key; self.kbd.set_key(KEYS[key]); self.send()
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

    def execute_mouse(self, command, request):
        if not self.mouse:
            raise ValueError("mouse interface was not configured by boot.py")
        if command == "mouse_move":
            dx = checked_int(request, "dx", 0, -127, 127)
            dy = checked_int(request, "dy", 0, -127, 127)
            self.send_mouse(dx, dy, 0)
        elif command == "mouse_report":
            self.mouse.buttons = checked_int(request, "buttons", self.mouse.buttons, 0, 7)
            self.send_mouse(checked_int(request, "dx", 0, -127, 127), checked_int(request, "dy", 0, -127, 127),
                            checked_int(request, "wheel", 0, -127, 127), checked_int(request, "pan", 0, -127, 127))
        elif command == "mouse_stream":
            # `count` identical reports, `interval_ms` apart. With interval 0
            # each report goes as soon as the host has taken the previous one,
            # which is as fast as the host polls. `key`, when given, is held on
            # the keyboard for the whole stream.
            dx = checked_int(request, "dx", 0, -127, 127)
            dy = checked_int(request, "dy", 0, -127, 127)
            wheel = checked_int(request, "wheel", 0, -127, 127)
            pan = checked_int(request, "pan", 0, -127, 127)
            count = checked_int(request, "count", 1, 1, 2000)
            interval = checked_int(request, "interval_ms", 0, 0, 1000)
            self.mouse.buttons = checked_int(request, "buttons", self.mouse.buttons, 0, 7)
            key = request.get("key")
            if key is not None and key not in KEYS:
                raise ValueError("unsupported key")
            if key is not None:
                self.held = key; self.kbd.set_key(KEYS[key]); self.send()
            started = time.ticks_ms()
            sent = 0
            try:
                for _ in range(count):
                    self.send_mouse(dx, dy, wheel, pan)
                    sent += 1
                    if interval:
                        self.wait(interval)
                    else:
                        self.kbd.service_idle()
            finally:
                self.last_stream = {"sent": sent, "elapsed_ms": time.ticks_diff(time.ticks_ms(), started)}
                if key is not None:
                    self.release_all()
        elif command == "mouse_buttons":
            self.mouse.buttons = checked_int(request, "buttons", 0, 0, 7)
            self.send_mouse(0, 0, 0)
        elif command == "mouse_wheel":
            # One report per detent, as a wheel with notches sends them.
            # Positive vertical is away from the user, positive horizontal is
            # to the right.
            vertical = checked_int(request, "vertical", 0, -64, 64)
            horizontal = checked_int(request, "horizontal", 0, -64, 64)
            gap = checked_int(request, "gap_ms", 150, 0, 2000)
            for _ in range(abs(vertical)):
                self.send_mouse(0, 0, 1 if vertical > 0 else -1)
                self.wait(gap)
            for _ in range(abs(horizontal)):
                self.send_mouse(0, 0, 0, 1 if horizontal > 0 else -1)
                self.wait(gap)
        else:
            raise ValueError("unknown command")

    def send_mouse(self, dx, dy, wheel, pan=0):
        if not self.mouse.send(dx, dy, wheel, pan):
            raise RuntimeError("mouse report was not accepted by the host")


def checked_int(request, name, default, low, high):
    value = request.get(name, default)
    if not isinstance(value, int) or value < low or value > high:
        raise ValueError("%s outside %d..%d" % (name, low, high))
    return value


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
