"""Configure the U64 HID keyboard before MicroPython enumerates USB.

The U64 host sees the firmware's default CDC device if HID is configured from
main.py.  It retains that failed device slot when the Pico re-enumerates, so
the keyboard must be installed during boot instead.
"""
import u64_hid_keyboard


u64_hid_keyboard.keyboard = u64_hid_keyboard.U64Keyboard()
u64_hid_keyboard.keyboard.start()
