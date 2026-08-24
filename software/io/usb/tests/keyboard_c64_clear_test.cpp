#include "host_test/host_test.h"
#include "keyboard_c64.h"
#include "keyboard_usb.h"

// keyboard_c64.cc calls it from getch() and wait_free(); neither is used here.
extern "C" void wait_ms(int)
{
}

namespace {

Keyboard_C64 makeKeyboard(volatile uint8_t *registers)
{
	return Keyboard_C64(NULL, registers, registers + 1, registers + 2);
}

} // namespace

TEST(KeyboardC64ClearTest, ClearBufferDropsPendingUsbKeystrokes)
{
	volatile uint8_t registers[3] = { 0xFF, 0xFF, 0xFF };
	Keyboard_C64 keyboard = makeKeyboard(registers);
	uint8_t press[USB_DATA_SIZE]   = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t release[USB_DATA_SIZE] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	system_usb_keyboard.clear_buffer();
	system_usb_keyboard.process_data(press);
	system_usb_keyboard.process_data(release);

	// Keyboard_C64::getch() falls through to the USB keyboard, so a clear that
	// leaves the USB queue alone lets an old keystroke arrive afterwards.
	keyboard.clear_buffer();
	EXPECT_EQ(-1, system_usb_keyboard.getch());
}

TEST(KeyboardC64ClearTest, ClearBufferKeepsInjectedKeys)
{
	volatile uint8_t registers[3] = { 0xFF, 0xFF, 0xFF };
	Keyboard_C64 keyboard = makeKeyboard(registers);

	system_usb_keyboard.clear_buffer();
	system_usb_keyboard.push_head(KEY_F1);

	// A key the input API injected is in flight, not old input, so a popup
	// clearing the buffer must not swallow it.
	keyboard.clear_buffer();
	EXPECT_TRUE(system_usb_keyboard.has_injected_key(KEY_F1));
	EXPECT_EQ(KEY_F1, system_usb_keyboard.getch());
}
