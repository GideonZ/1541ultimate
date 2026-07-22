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
	uint8_t brk_report[USB_DATA_SIZE] = { 0x01, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00 };

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
	keyboard.process_data(release);

	keyboard.process_data(brk_report);
	EXPECT_EQ(KEY_CTRL_R, keyboard.getch());
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

// USB HID usage 0x2C (space) maps to C64 matrix row 7, column 4.
static const uint8_t USB_KEY_SPACE = 0x2C;
static const uint8_t MATRIX_SPACE_ROW = 7;
static const uint8_t MATRIX_SPACE_BIT = 0x10;

TEST(KeyboardUsbMatrixTest, KeyReleasedWhileMatrixDisabledDoesNotStick)
{
	Keyboard_USB keyboard;
	uint8_t matrix[11] = { 0 };
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, USB_KEY_SPACE, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t release[USB_DATA_SIZE] = { 0x00 };

	keyboard.setMatrix(matrix);
	keyboard.enableMatrix(true);

	keyboard.process_data(press);
	EXPECT_EQ(MATRIX_SPACE_BIT, matrix[MATRIX_SPACE_ROW]);

	keyboard.enableMatrix(false); // menu opens while the key is still held
	EXPECT_EQ(0x00, matrix[MATRIX_SPACE_ROW]);

	keyboard.process_data(release); // key released with the menu still open
	keyboard.enableMatrix(true);    // menu closes

	EXPECT_EQ(0x00, matrix[MATRIX_SPACE_ROW]);
}

TEST(KeyboardUsbMatrixTest, ShiftReleasedWhileMatrixDisabledDoesNotStick)
{
	Keyboard_USB keyboard;
	uint8_t matrix[11] = { 0 };
	uint8_t left_shift[USB_DATA_SIZE] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t release[USB_DATA_SIZE] = { 0x00 };

	keyboard.setMatrix(matrix);
	keyboard.enableMatrix(true);

	keyboard.process_data(left_shift);
	EXPECT_EQ(0x80, matrix[1]);

	keyboard.enableMatrix(false);
	keyboard.process_data(release);
	keyboard.enableMatrix(true);

	EXPECT_EQ(0x00, matrix[1]);
}

TEST(KeyboardUsbMatrixTest, KeyStillHeldWhenMatrixIsEnabledIsReported)
{
	Keyboard_USB keyboard;
	uint8_t matrix[11] = { 0 };
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, USB_KEY_SPACE, 0x00, 0x00, 0x00, 0x00, 0x00 };

	keyboard.setMatrix(matrix);
	keyboard.enableMatrix(false);

	keyboard.process_data(press); // pressed while the menu owns the keyboard
	EXPECT_EQ(0x00, matrix[MATRIX_SPACE_ROW]);

	keyboard.enableMatrix(true); // still physically held when the menu closes
	EXPECT_EQ(MATRIX_SPACE_BIT, matrix[MATRIX_SPACE_ROW]);
}

TEST(KeyboardUsbMatrixTest, KeysTypedWhileMatrixDisabledDoNotReachTheMatrix)
{
	Keyboard_USB keyboard;
	uint8_t matrix[11] = { 0 };
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, USB_KEY_SPACE, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t release[USB_DATA_SIZE] = { 0x00 };

	keyboard.setMatrix(matrix);
	keyboard.enableMatrix(false);

	keyboard.process_data(press);
	EXPECT_EQ(0x00, matrix[MATRIX_SPACE_ROW]);
	keyboard.process_data(release);
	EXPECT_EQ(0x00, matrix[MATRIX_SPACE_ROW]);

	keyboard.enableMatrix(true);
	EXPECT_EQ(0x00, matrix[MATRIX_SPACE_ROW]);
}

TEST(KeyboardUsbMatrixTest, MenuKeystrokesDoNotLeakToTheMatrixAfterTheMenuCloses)
{
	Keyboard_USB keyboard;
	uint8_t matrix[11] = { 0 };

	keyboard.setMatrix(matrix);
	keyboard.enableMatrix(true);

	keyboard.enableMatrix(false); // menu takes the keyboard
	keyboard.push_head(KEY_UP);   // menu navigation (mouse wheel / REST)
	keyboard.push_head(KEY_DOWN);
	keyboard.enableMatrix(true);  // menu hands it back

	EXPECT_EQ(-1, keyboard.getch());
	EXPECT_EQ(0x00, matrix[0]);
	EXPECT_EQ(0x00, matrix[6]);
}

TEST(KeyboardUsbMatrixTest, CursorKeysQueuedWhileMatrixEnabledStillReachTheMatrix)
{
	Keyboard_USB keyboard;
	uint8_t matrix[11] = { 0 };

	keyboard.setMatrix(matrix);
	keyboard.enableMatrix(true);
	keyboard.push_head(KEY_UP); // mouse cursor mode drives the C64 this way

	EXPECT_EQ(KEY_UP, keyboard.getch());
	EXPECT_EQ(0x80, matrix[0]);
	EXPECT_EQ(0x10, matrix[6]);
}

TEST(KeyboardUsbMatrixTest, ResetRestoreAndFreezeAreGatedAndNotStale)
{
	Keyboard_USB keyboard;
	uint8_t matrix[11] = { 0 };
	uint8_t reset_combo[USB_DATA_SIZE] = { 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t restore[USB_DATA_SIZE] = { 0x00, 0x00, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00 }; // F12
	uint8_t freeze[USB_DATA_SIZE] = { 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00 };  // F11
	uint8_t release[USB_DATA_SIZE] = { 0x00 };

	keyboard.setMatrix(matrix);
	keyboard.enableMatrix(true);

	keyboard.process_data(reset_combo);
	EXPECT_EQ(0x01, matrix[8]);
	keyboard.enableMatrix(false);
	EXPECT_EQ(0x00, matrix[8]); // reset must not stay asserted while the menu is open
	keyboard.process_data(release);
	keyboard.enableMatrix(true);
	EXPECT_EQ(0x00, matrix[8]);

	keyboard.process_data(restore);
	EXPECT_EQ(0x01, matrix[9]);
	keyboard.enableMatrix(false);
	keyboard.process_data(release);
	keyboard.enableMatrix(true);
	EXPECT_EQ(0x00, matrix[9]);

	keyboard.process_data(freeze);
	EXPECT_EQ(0x01, matrix[10]);
	keyboard.enableMatrix(false);
	keyboard.process_data(release);
	keyboard.enableMatrix(true);
	EXPECT_EQ(0x00, matrix[10]);
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
