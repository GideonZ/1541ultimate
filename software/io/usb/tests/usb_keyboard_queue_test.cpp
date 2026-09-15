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

// HID usage 0x15 is R and 0x51 is the down arrow. R's ASCII control code is
// 0x12, which is also KEY_DOWN, so if the control map carried that code the
// monitor could not tell the reset shortcut from cursor-down. KEY_CTRL_R is
// outside the ASCII range for that reason.
TEST(KeyboardUsbQueueTest, ControlRIsDistinctFromCursorDown)
{
	Keyboard_USB keyboard;
	uint8_t ctrl_r[USB_DATA_SIZE] = { 0x01, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t down[USB_DATA_SIZE] = { 0x00, 0x00, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t release[USB_DATA_SIZE] = { 0x00 };

	EXPECT_TRUE(KEY_CTRL_R != KEY_DOWN);

	keyboard.process_data(ctrl_r);
	EXPECT_EQ(KEY_CTRL_R, keyboard.getch());
	keyboard.process_data(release);

	keyboard.process_data(down);
	EXPECT_EQ(KEY_DOWN, keyboard.getch());
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
	keyboard.push_head_repeat(KEY_UP, USB_INJECTED_BUFFER_SIZE + 8);
	keyboard.process_data(release);

	for (int i = 0; i < USB_INJECTED_BUFFER_SIZE - 1; i++) {
		EXPECT_EQ(KEY_UP, keyboard.getch());
	}
	EXPECT_EQ('a', keyboard.getch());
	EXPECT_EQ(-1, keyboard.getch());
}

// The input API accepts a batch of 64 keyboard events and the menu path pushes
// each of them into the injected ring. The ring keeps one slot empty to tell a
// full ring from an empty one, so a 64-entry ring held only 63 keys and dropped
// the 64th without an error. The ring is one slot larger than the batch limit
// so a full batch arrives complete.
TEST(KeyboardUsbQueueTest, AFullInputApiBatchIsNotDropped)
{
	static const int INPUT_API_BATCH = 64;
	Keyboard_USB keyboard;

	EXPECT_TRUE(USB_INJECTED_BUFFER_SIZE > INPUT_API_BATCH);

	for (int i = 0; i < INPUT_API_BATCH; i++) {
		keyboard.push_head(KEY_UP);
	}
	EXPECT_EQ(INPUT_API_BATCH, keyboard.count_injected_key(KEY_UP));

	for (int i = 0; i < INPUT_API_BATCH; i++) {
		EXPECT_EQ(KEY_UP, keyboard.getch());
	}
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

TEST(KeyboardUsbMatrixTest, AnyKeyPressedTracksTheLiveReport)
{
	Keyboard_USB keyboard;
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, USB_KEY_SPACE, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t left_shift[USB_DATA_SIZE] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t release[USB_DATA_SIZE] = { 0x00 };

	EXPECT_FALSE(keyboard.anyKeyPressed());

	keyboard.process_data(press);
	EXPECT_TRUE(keyboard.anyKeyPressed());
	keyboard.process_data(release);
	EXPECT_FALSE(keyboard.anyKeyPressed());

	keyboard.process_data(left_shift); // modifier only still counts as held
	EXPECT_TRUE(keyboard.anyKeyPressed());
	keyboard.process_data(release);
	EXPECT_FALSE(keyboard.anyKeyPressed());
}

TEST(KeyboardUsbMatrixTest, WaitFreeGivesUpOnAKeyThatIsNeverReleased)
{
	Keyboard_USB keyboard;
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, USB_KEY_SPACE, 0x00, 0x00, 0x00, 0x00, 0x00 };

	keyboard.process_data(press);
	keyboard.wait_free(); // must return instead of spinning forever
	EXPECT_TRUE(keyboard.anyKeyPressed());
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

// A menu UI object polls getch() every 20ms. first_delay of 16 polls holds the
// repeat off for the first 320ms, and repeat_speed of 4 then produces one
// repeated character every fifth poll, so about 97 over a ten second run.
static const int UI_POLL_MS = 20;
static const int UI_POLLS_PER_RUN = 500; // 10 seconds of polling
static const int USB_IDLE_PERIOD_MS = 100;
static const int UI_POLLS_PER_IDLE_PERIOD = USB_IDLE_PERIOD_MS / UI_POLL_MS;
static const int UNCEILED_REPEATS = 90;
// The ceiling is three idle periods, so it can let through at most a few repeats.
static const int BOUNDED_REPEATS = 4;

static int poll_ui(Keyboard_USB& keyboard, int key, int polls, const uint8_t *periodic_report)
{
	int received = 0;
	for (int i = 0; i < polls; i++) {
		host_test_advance_ms_timer(UI_POLL_MS);
		if (periodic_report && ((i % UI_POLLS_PER_IDLE_PERIOD) == (UI_POLLS_PER_IDLE_PERIOD - 1))) {
			keyboard.process_data(const_cast<uint8_t *>(periodic_report));
		}
		if (keyboard.getch() == key) {
			received++;
		}
	}
	return received;
}

TEST(KeyboardUsbRepeatTest, LostReleaseDoesNotFreeRunTheRepeat)
{
	Keyboard_USB keyboard;
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };

	host_test_set_ms_timer(0);
	keyboard.setReportIdlePeriod(USB_IDLE_PERIOD_MS);

	keyboard.process_data(press);
	EXPECT_EQ('a', keyboard.getch());

	// The release report never arrives, so num_keys stays at 1 forever.
	EXPECT_TRUE(poll_ui(keyboard, 'a', UI_POLLS_PER_RUN, NULL) <= BOUNDED_REPEATS);
}

TEST(KeyboardUsbRepeatTest, HeldKeyWithPeriodicReportsKeepsRepeating)
{
	Keyboard_USB keyboard;
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };

	host_test_set_ms_timer(0);
	keyboard.setReportIdlePeriod(USB_IDLE_PERIOD_MS);

	keyboard.process_data(press);
	EXPECT_EQ('a', keyboard.getch());

	// The negotiated idle rate re-reports the key every 100ms while it is held.
	EXPECT_TRUE(poll_ui(keyboard, 'a', UI_POLLS_PER_RUN, press) > UNCEILED_REPEATS);
}

TEST(KeyboardUsbRepeatTest, KeyboardWithoutAnIdleRateRepeatsAsBefore)
{
	Keyboard_USB keyboard;
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };

	host_test_set_ms_timer(0);
	keyboard.setReportIdlePeriod(0); // SET_IDLE stalled or was ignored
	EXPECT_EQ(0, keyboard.reportIdlePeriod());

	keyboard.process_data(press);
	EXPECT_EQ('a', keyboard.getch());

	// No reports arrive while the key is held, so silence must not stop the repeat.
	EXPECT_TRUE(poll_ui(keyboard, 'a', UI_POLLS_PER_RUN, NULL) > UNCEILED_REPEATS);
}

TEST(KeyboardUsbRepeatTest, RepeatResumesWhenReportsComeBack)
{
	Keyboard_USB keyboard;
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };

	host_test_set_ms_timer(0);
	keyboard.setReportIdlePeriod(USB_IDLE_PERIOD_MS);

	keyboard.process_data(press);
	EXPECT_EQ('a', keyboard.getch());

	// A long enough gap in the periodic reports stops the repeat, but the key is
	// still down and the reports come back, so the repeat has to come back too.
	EXPECT_TRUE(poll_ui(keyboard, 'a', 50, NULL) <= BOUNDED_REPEATS);
	EXPECT_TRUE(poll_ui(keyboard, 'a', UI_POLLS_PER_RUN, press) > UNCEILED_REPEATS);
}

TEST(KeyboardUsbRepeatTest, StaleRepeatStaysOffWhenTheMillisecondTimerWraps)
{
	Keyboard_USB keyboard;
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };

	// The report is taken just below the point where the 16-bit timer wraps.
	const uint16_t last_report_time = 0xFFF0;
	host_test_set_ms_timer(last_report_time);
	keyboard.setReportIdlePeriod(USB_IDLE_PERIOD_MS);

	keyboard.process_data(press);
	EXPECT_EQ('a', keyboard.getch());

	// The release report never arrives, so the repeat reaches the ceiling.
	const int STALE_POLLS = 50;
	EXPECT_TRUE(poll_ui(keyboard, 'a', STALE_POLLS, NULL) <= BOUNDED_REPEATS);

	// The timer now runs a full 16-bit cycle back to the time of the last report.
	// The difference between the two is zero again, so without the latched stale
	// flag the ancient report would look like it had just arrived and the repeat
	// would free run for another three idle periods on every wrap.
	host_test_advance_ms_timer((uint16_t)(0x10000 - (STALE_POLLS * UI_POLL_MS)));
	EXPECT_EQ(last_report_time, getMsTimer());
	EXPECT_EQ(0, poll_ui(keyboard, 'a', UI_POLLS_PER_RUN, NULL));

	// A real report clears the latch, so the repeat still recovers after a wrap.
	keyboard.process_data(press);
	EXPECT_TRUE(poll_ui(keyboard, 'a', UI_POLLS_PER_RUN, press) > UNCEILED_REPEATS);
}

TEST(KeyboardUsbRepeatTest, ClearBufferStopsARepeatThatIsRunning)
{
	Keyboard_USB keyboard;
	uint8_t press[USB_DATA_SIZE] = { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };

	host_test_set_ms_timer(0);
	keyboard.process_data(press);
	EXPECT_EQ('a', keyboard.getch());

	// Let the initial delay run out so the repeat is emitting.
	EXPECT_TRUE(poll_ui(keyboard, 'a', 40, NULL) > 0);

	keyboard.clear_buffer();

	// The key is still held, so the repeat has to serve the initial delay again
	// instead of emitting into the buffer that was just cleared.
	EXPECT_EQ(0, poll_ui(keyboard, 'a', 16, NULL));
	EXPECT_EQ('a', keyboard.getch());
}
