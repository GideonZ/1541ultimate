"""The deliberately small keyboard-only HID device used by the soak test."""
import time
import usb.device
from usb.device.hid import HIDInterface


# HID boot keyboard: modifier, reserved, and six key slots.  There is exactly
# one application collection, so U64 may safely negotiate report-id-zero idle.
_REPORT_DESC = (
    b"\x05\x01\x09\x06\xa1\x01\x05\x07\x19\xe0\x29\xe7"
    b"\x15\x00\x25\x01\x75\x01\x95\x08\x81\x02\x95\x01"
    # F13 (0x68) is used only by the harmless Linux-side self-test.
    b"\x75\x08\x81\x01\x95\x06\x75\x08\x15\x00\x25\x68"
    b"\x05\x07\x19\x00\x29\x68\x81\x00\xc0"
)


class U64Keyboard(HIDInterface):
    def __init__(self):
        HIDInterface.__init__(self, _REPORT_DESC, protocol=1,
                              interface_str="U64 soak keyboard", interval_ms=8)
        self.report = bytearray(8)
        self.reports_sent = 0
        self._last_sent = time.ticks_ms()

    def start(self):
        # Configure before USB comes up (from boot.py), so the U64 sees this
        # HID interface during its *first* enumeration.  Retain CDC for
        # provisioning/recovery: U64 walks every interface and claims this
        # keyboard-only HID interface (class 03) while ignoring CDC (02/0A).
        usb.device.get().init(self, builtin_driver=True)

    def set_key(self, key):
        self.report[2] = key or 0
        for index in range(3, 8):
            self.report[index] = 0

    def send_now(self):
        if self.send_report(self.report):
            self.reports_sent += 1
            self._last_sent = time.ticks_ms()
            return True
        return False

    def service_idle(self, suppressed=False):
        # HID idle is in 4 ms units. HIDInterface stores it after SET_IDLE and
        # answers GET_IDLE itself; this is the required data-plane behaviour.
        if suppressed or not self.idle_rate:
            return
        period = self.idle_rate * 4
        if time.ticks_diff(time.ticks_ms(), self._last_sent) >= period:
            self.send_now()


# Assigned by boot.py.  Keeping it here avoids importing boot.py a second time
# when main.py starts.
keyboard = None
