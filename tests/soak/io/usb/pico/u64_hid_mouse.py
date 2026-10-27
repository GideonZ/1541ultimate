"""The small wheel mouse HID interface used by the USB mouse tests."""
import struct
from usb.device.hid import HIDInterface


# Three buttons, then relative X, Y, vertical wheel and horizontal wheel (AC
# Pan) of one byte each.  The U64 reads it through its report descriptor
# parser rather than the boot protocol, as it reads most wheel mice.
_REPORT_DESC = (
    b"\x05\x01\x09\x02\xa1\x01\x09\x01\xa1\x00"
    b"\x05\x09\x19\x01\x29\x03\x15\x00\x25\x01\x95\x03\x75\x01\x81\x02"
    b"\x95\x01\x75\x05\x81\x01"
    b"\x05\x01\x09\x30\x09\x31\x09\x38\x15\x81\x25\x7f\x75\x08\x95\x03\x81\x06"
    b"\x05\x0c\x0a\x38\x02\x15\x81\x25\x7f\x75\x08\x95\x01\x81\x06"
    b"\xc0\xc0"
)


class U64Mouse(HIDInterface):
    def __init__(self):
        HIDInterface.__init__(self, _REPORT_DESC, protocol=2,
                              interface_str="U64 test mouse", interval_ms=8)
        self.report = bytearray(5)
        self.buttons = 0
        self.reports_sent = 0

    def send(self, dx=0, dy=0, wheel=0, pan=0):
        struct.pack_into("Bbbbb", self.report, 0, self.buttons, dx, dy, wheel, pan)
        if self.send_report(self.report):
            self.reports_sent += 1
            return True
        return False
