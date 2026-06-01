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

TEST(KeyboardUsbQueueTest, ControlBookmarkDigitsStayDistinctFromRecall)
{
	Keyboard_USB keyboard;
	uint8_t recall_report[USB_DATA_SIZE] = { 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t set_slot_report[USB_DATA_SIZE] = { 0x01, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t release[USB_DATA_SIZE] = { 0x00 };
	uint8_t list_report[USB_DATA_SIZE] = { 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t edit_report[USB_DATA_SIZE] = { 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 };

	keyboard.process_data(recall_report);
	EXPECT_EQ('1', keyboard.getch());
	keyboard.process_data(release);

	keyboard.process_data(set_slot_report);
	EXPECT_EQ(KEY_CTRL_1, keyboard.getch());
	keyboard.process_data(release);

	keyboard.process_data(list_report);
	EXPECT_EQ(KEY_CTRL_B, keyboard.getch());
	keyboard.process_data(release);

	keyboard.process_data(edit_report);
	EXPECT_EQ(KEY_CTRL_E, keyboard.getch());
}

TEST(KeyboardUsbQueueTest, CbmDigitDecodeRejectsInvalidKeys)
{
	EXPECT_TRUE(key_is_ctrl_digit(KEY_CTRL_0));
	EXPECT_TRUE(key_is_ctrl_digit(KEY_CTRL_9));
	EXPECT_FALSE(key_is_ctrl_digit('0'));
	EXPECT_FALSE(key_is_ctrl_digit(KEY_CTRL_B));
	EXPECT_EQ(0, key_ctrl_digit_value(KEY_CTRL_0));
	EXPECT_EQ(9, key_ctrl_digit_value(KEY_CTRL_9));
	EXPECT_EQ(-1, key_ctrl_digit_value('0'));
	EXPECT_EQ(-1, key_ctrl_digit_value(KEY_CTRL_B));
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

TEST(KeyboardUsbQueueTest, RemoveInjectedKeyDropsPendingDirection)
{
	Keyboard_USB keyboard;

	keyboard.push_head(KEY_DOWN);
	keyboard.push_head(KEY_DOWN);
	keyboard.push_head(KEY_UP);
	EXPECT_EQ(2, keyboard.count_injected_key(KEY_DOWN));
	EXPECT_EQ(1, keyboard.count_injected_key(KEY_UP));
	keyboard.remove_injected_key(KEY_DOWN);

	EXPECT_TRUE(keyboard.has_injected_key(KEY_UP));
	EXPECT_FALSE(keyboard.has_injected_key(KEY_DOWN));
	EXPECT_EQ(1, keyboard.count_injected_key(KEY_UP));
	EXPECT_EQ(0, keyboard.count_injected_key(KEY_DOWN));
	EXPECT_EQ(KEY_UP, keyboard.getch());
	EXPECT_EQ(-1, keyboard.getch());
}
