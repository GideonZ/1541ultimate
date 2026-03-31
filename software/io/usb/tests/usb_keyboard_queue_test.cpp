#include "host_test/host_test.h"
#include "../keyboard_usb.h"

TEST(KeyboardUsbQueueTest, PushHeadPrependsInjectedKey)
{
	Keyboard_USB keyboard;
	uint8_t report[USB_DATA_SIZE] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };

	keyboard.process_data(report);
	keyboard.push_head(KEY_UP);

	EXPECT_EQ(KEY_UP, keyboard.getch());
	EXPECT_EQ('a', keyboard.getch());
	EXPECT_EQ(-1, keyboard.getch());
}

TEST(KeyboardUsbQueueTest, PushHeadRepeatIsBounded)
{
	Keyboard_USB keyboard;
	uint8_t report[USB_DATA_SIZE] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t release[USB_DATA_SIZE] = { 0x00 };

	keyboard.process_data(report);
	keyboard.push_head_repeat(KEY_UP, USB_KEY_BUFFER_SIZE + 8);
	keyboard.process_data(release);

	for (int i = 0; i < USB_KEY_BUFFER_SIZE - 1; i++) {
		EXPECT_EQ(KEY_UP, keyboard.getch());
	}
	EXPECT_EQ('a', keyboard.getch());
	EXPECT_EQ(-1, keyboard.getch());
}

TEST(KeyboardUsbQueueTest, InjectedCursorKeyPulsesMatrix)
{
	Keyboard_USB keyboard;
	uint8_t matrix[11] = { 0 };

	keyboard.setMatrix(matrix);
	keyboard.enableMatrix(true);
	keyboard.push_head(KEY_UP);

	EXPECT_EQ(KEY_UP, keyboard.getch());
	EXPECT_EQ(0x80, matrix[0]);
	EXPECT_EQ(0x10, matrix[6]);

	EXPECT_EQ(-1, keyboard.getch());
	EXPECT_EQ(0x00, matrix[0]);
	EXPECT_EQ(0x00, matrix[6]);
}