#include "host_test/host_test.h"
#include "../hid_decoder.h"

namespace {

const uint8_t kMouse3ButtonDescriptor[] = {
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01,
	0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
	0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01,
	0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81,
	0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
	0xC0, 0xC0,
};

const uint8_t kMouseWheelDescriptor[] = {
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01,
	0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
	0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01,
	0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
	0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
	0x81, 0x06, 0xC0, 0xC0,
};

const uint8_t kMouseHorizontalWheelDescriptor[] = {
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01,
	0xA1, 0x00, 0x85, 0x01, 0x05, 0x09, 0x19, 0x01,
	0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
	0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
	0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
	0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
	0x95, 0x03, 0x81, 0x06, 0x85, 0x02, 0x05, 0x0C,
	0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F, 0x75,
	0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
};

const uint8_t kKeyboardDescriptor[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
	0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
	0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08,
	0x15, 0x00, 0x25, 0x65, 0x19, 0x00, 0x29, 0x65,
	0x81, 0x00, 0xC0,
};

} // namespace

TEST(HidMouseDescriptorTest, ParseStandardThreeButtonMouse)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kMouse3ButtonDescriptor, sizeof(kMouse3ButtonDescriptor)));
	ASSERT_TRUE(items.locateMouseFields(fields));
	EXPECT_TRUE(fields.has_button1);
	EXPECT_TRUE(fields.has_button2);
	EXPECT_TRUE(fields.has_button3);
	EXPECT_FALSE(fields.has_wheel_v);
	EXPECT_FALSE(fields.has_wheel_h);

	const uint8_t report[] = { 0x05, 0x10, 0xF0 };
	EXPECT_NE(0, HidReport::getValueFromData(report, fields.button1));
	EXPECT_EQ(0, HidReport::getValueFromData(report, fields.button2));
	EXPECT_NE(0, HidReport::getValueFromData(report, fields.button3));
	EXPECT_EQ(16, HidReport::getValueFromData(report, fields.mouse_x));
	EXPECT_EQ(-16, HidReport::getValueFromData(report, fields.mouse_y));
}

TEST(HidMouseDescriptorTest, ParseFiveButtonMouseWithWheel)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kMouseWheelDescriptor, sizeof(kMouseWheelDescriptor)));
	ASSERT_TRUE(items.locateMouseFields(fields));
	EXPECT_TRUE(fields.has_wheel_v);
	EXPECT_FALSE(fields.has_wheel_h);

	const uint8_t report[] = { 0x11, 0x02, 0xFE, 0x01 };
	EXPECT_NE(0, HidReport::getValueFromData(report, fields.button1));
	EXPECT_EQ(2, HidReport::getValueFromData(report, fields.mouse_x));
	EXPECT_EQ(-2, HidReport::getValueFromData(report, fields.mouse_y));
	EXPECT_EQ(1, HidReport::getValueFromData(report, fields.wheel_v));
}

TEST(HidMouseDescriptorTest, DetectNonBootMouseFromDescriptor)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kMouse3ButtonDescriptor, sizeof(kMouse3ButtonDescriptor)));
	ASSERT_TRUE(items.locateMouseFields(fields));
	EXPECT_TRUE(fields.valid);
	EXPECT_TRUE(fields.has_button1);
	EXPECT_EQ(8, fields.mouse_x.length);
	EXPECT_EQ(8, fields.mouse_y.length);
	EXPECT_EQ(-127, fields.mouse_x.min);
}

TEST(HidMouseDescriptorTest, RejectsMouseDescriptorMissingYAxis)
{
	const uint8_t descriptor_missing_y[] = {
		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01,
		0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
		0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01,
		0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
		0x05, 0x01, 0x09, 0x30, 0x15, 0x81, 0x25, 0x7F,
		0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
	};

	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;
	ASSERT_EQ(0, parser.decode(&items, descriptor_missing_y, sizeof(descriptor_missing_y)));
	EXPECT_FALSE(items.locateMouseFields(fields));
	EXPECT_FALSE(fields.valid);
}

TEST(HidMouseDescriptorTest, RejectsMouseDescriptorWithAbsoluteAxes)
{
	const uint8_t absolute_axis_descriptor[] = {
		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01,
		0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
		0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01,
		0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
		0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00,
		0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81,
		0x02, 0xC0, 0xC0,
	};

	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;
	ASSERT_EQ(0, parser.decode(&items, absolute_axis_descriptor, sizeof(absolute_axis_descriptor)));
	EXPECT_FALSE(items.locateMouseFields(fields));
	EXPECT_FALSE(fields.valid);
}

TEST(HidMouseDescriptorTest, RejectsTruncatedDescriptor)
{
	HidReportParser parser;
	HidItemList items;
	EXPECT_NE(0, parser.decode(&items, kMouse3ButtonDescriptor, 3));
}

TEST(HidMouseDescriptorTest, RejectsEmptyDescriptor)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kMouse3ButtonDescriptor, 0));
	EXPECT_FALSE(items.locateMouseFields(fields));
}

TEST(HidMouseDescriptorTest, ReusedItemListDoesNotRetainPreviousReports)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;

	ASSERT_EQ(0, parser.decode(&items, kMouse3ButtonDescriptor, sizeof(kMouse3ButtonDescriptor)));
	ASSERT_TRUE(items.locateMouseFields(fields));
	EXPECT_TRUE(fields.valid);

	ASSERT_EQ(0, parser.decode(&items, kKeyboardDescriptor, sizeof(kKeyboardDescriptor)));
	EXPECT_FALSE(items.locateMouseFields(fields));
	EXPECT_FALSE(fields.valid);
}

TEST(HidMouseDescriptorTest, ParseHorizontalWheelAcPan)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kMouseHorizontalWheelDescriptor, sizeof(kMouseHorizontalWheelDescriptor)));
	ASSERT_TRUE(items.locateMouseFields(fields));
	EXPECT_TRUE(fields.has_wheel_v);
	EXPECT_TRUE(fields.has_wheel_h);
	EXPECT_EQ(1, fields.mouse_x.report);
	EXPECT_EQ(2, fields.wheel_h.report);

	const uint8_t axis_report[] = { 0x01, 0x01, 0x04, 0xFC, 0x01 };
	const uint8_t pan_report[] = { 0x02, 0xFD };
	EXPECT_EQ(4, HidReport::getValueFromData(axis_report, sizeof(axis_report), fields.mouse_x));
	EXPECT_EQ(-4, HidReport::getValueFromData(axis_report, sizeof(axis_report), fields.mouse_y));
	EXPECT_EQ(1, HidReport::getValueFromData(axis_report, sizeof(axis_report), fields.wheel_v));
	EXPECT_EQ(-3, HidReport::getValueFromData(pan_report, sizeof(pan_report), fields.wheel_h));
}

TEST(HidMouseInterpreterTest, AppliesScrollFactorAndRuntimeChanges)
{
	int16_t mouse_x = 10;
	int16_t mouse_y = 20;
	int axis_vertical_remainder = 0;
	int key_vertical_remainder = 0;
	HidMouseInterpreter::applyRelativeMotion(mouse_x, mouse_y, 3, -4);
	EXPECT_EQ(13, mouse_x);
	EXPECT_EQ(24, mouse_y);

	EXPECT_EQ(-32, HidMouseInterpreter::scaleHorizontalWheel(-2, 8));
	EXPECT_EQ(2, HidMouseInterpreter::scaleVerticalWheel(1, 8, axis_vertical_remainder));
	EXPECT_EQ(16, axis_vertical_remainder);
	HidMouseInterpreter::applyWheelAxisDeltas(mouse_x, mouse_y, -32, 2);
	EXPECT_EQ(-19, mouse_x);
	EXPECT_EQ(22, mouse_y);

	EXPECT_EQ(12, HidMouseInterpreter::scaleHorizontalWheelKeys(1, 8));
	EXPECT_EQ(-12, HidMouseInterpreter::scaleHorizontalWheelKeys(-1, 8));
	EXPECT_EQ(2, HidMouseInterpreter::scaleVerticalWheelKeys(1, 8, key_vertical_remainder));
	EXPECT_EQ(0, key_vertical_remainder);

	EXPECT_EQ(0, HidMouseInterpreter::scaleVerticalWheelKeys(-1, 2, key_vertical_remainder));
	EXPECT_EQ(-2, key_vertical_remainder);
	EXPECT_EQ(-1, HidMouseInterpreter::scaleVerticalWheelKeys(-1, 2, key_vertical_remainder));
	EXPECT_EQ(0, key_vertical_remainder);
}

TEST(HidMouseInterpreterTest, HorizontalDirectionAndMenuWheelAreSymmetric)
{
	EXPECT_EQ(3, HidMouseInterpreter::normalizeHorizontalWheel(-3));
	EXPECT_EQ(3, HidMouseInterpreter::normalizeVerticalWheel(3));
	EXPECT_EQ(3, HidMouseInterpreter::applyWheelDirection(3, false));
	EXPECT_EQ(-3, HidMouseInterpreter::applyWheelDirection(3, true));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelKeys(2));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelKeys(-2));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelKeys(120));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelKeys(-120));
}

TEST(HidMouseInterpreterTest, SlowMenuWheelBurstAdvancesSingleStep)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 0, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 40, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 120, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
}

TEST(HidMouseInterpreterTest, SeparatedSlowMenuWheelNotchesStartNewBursts)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 0, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 90, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 180, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
}

TEST(HidMouseInterpreterTest, FastMenuWheelBurstAcceleratesModerately)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-1, 0, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(-5, 10, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(-5, 20, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-5, 30, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(-5, 90, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(-5, 100, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-5, 110, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
}

TEST(HidMouseInterpreterTest, HighResolutionSlowWheelDoesNotLeakPastOneStep)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 0, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 10, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 20, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 30, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 220, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
}

TEST(HidMouseInterpreterTest, MenuWheelResetAndDirectionChangesReturnToPreciseMode)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 0, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(5, 10, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_ACCELERATED, mode);
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-1, 20, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE, mode);
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-1, 200, last_tick, mode, burst_direction, burst_accumulator, 25, 75, 150, 15));
	EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE, mode);
}

TEST(HidBootProtocolTest, DecodesBootMouseReport)
{
	t_hid_boot_mouse_sample sample;
	const uint8_t report[] = { 0x05, 0x7F, 0x81 };
	HidBootProtocol::decodeMouseReport(report, sizeof(report), sample);
	EXPECT_EQ(0x05, sample.buttons);
	EXPECT_EQ(127, sample.x);
	EXPECT_EQ(-127, sample.y);
}

TEST(HidReportLengthTest, ShortReportDoesNotReadPastAvailableData)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_mouse_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kMouseHorizontalWheelDescriptor, sizeof(kMouseHorizontalWheelDescriptor)));
	ASSERT_TRUE(items.locateMouseFields(fields));

	const uint8_t short_axis_report[] = { 0x01, 0x01, 0x04 };
	EXPECT_TRUE(HidReport::hasValue(short_axis_report, sizeof(short_axis_report), fields.mouse_x));
	EXPECT_FALSE(HidReport::hasValue(short_axis_report, sizeof(short_axis_report), fields.mouse_y));
	EXPECT_EQ(4, HidReport::getValueFromData(short_axis_report, sizeof(short_axis_report), fields.mouse_x));
	EXPECT_EQ(0, HidReport::getValueFromData(short_axis_report, sizeof(short_axis_report), fields.mouse_y));
	EXPECT_EQ(0, HidReport::getValueFromData(short_axis_report, sizeof(short_axis_report), fields.wheel_v));
}

TEST(HidBootProtocolTest, PadsShortBootMouseReport)
{
	t_hid_boot_mouse_sample sample;
	const uint8_t report[] = { 0x01 };
	HidBootProtocol::decodeMouseReport(report, sizeof(report), sample);
	EXPECT_EQ(0x01, sample.buttons);
	EXPECT_EQ(0, sample.x);
	EXPECT_EQ(0, sample.y);
}

// Rebound suppression tests.
// Parameters: fast_gap=25, slow_gap=75, reset_gap=150, burst_extra_threshold=15
// Helper to reset per-call state for clarity.
namespace {
struct BurstState {
    uint32_t last_tick = 0;
    int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
    int burst_direction = 0;
    int burst_accumulator = 0;
    int call(int delta, uint32_t now) {
        return HidMouseInterpreter::scaleMenuWheelBurst(
            delta, now, last_tick, mode, burst_direction, burst_accumulator,
            25, 75, 150, 15);
    }
};
} // namespace

TEST(MenuWheelReboundTest, SlowDownwardReboundIsSuppressed)
{
    // Slow downward notch at t=0 commits one row down.
    // A tiny upward signal at t=50 (< slow_gap=75) is mechanical rebound —
    // it must not move the menu back up.
    BurstState s;
    EXPECT_EQ(1,  s.call(+1,  0));  // down: committed
    EXPECT_EQ(0,  s.call(-1, 50));  // rebound within slow_gap: suppressed
    EXPECT_EQ(0,  s.call(-1, 60));  // still within window: suppressed
}

TEST(MenuWheelReboundTest, SlowUpwardReboundIsSuppressed)
{
    // Mirror of downward: upward notch followed by small downward rebound.
    BurstState s;
    EXPECT_EQ(-1, s.call(-1,  0));  // up: committed
    EXPECT_EQ(0,  s.call(+1, 50));  // rebound within slow_gap: suppressed
}

TEST(MenuWheelReboundTest, LateReboundBeyondSlowGapStillSuppressedByResetGap)
{
    // A rebound arriving between slow_gap(75) and reset_gap(150) was previously
    // not caught; the extended window must suppress it.
    BurstState s;
    EXPECT_EQ(1,  s.call(+1,  0));  // down: committed, last_tick=0
    EXPECT_EQ(0,  s.call(-1, 80));  // late rebound at 80ms: now suppressed (80 < reset_gap=150)
    EXPECT_EQ(0,  s.call(-1, 100)); // still within window: suppressed
    EXPECT_EQ(0,  s.call(-1, 140)); // 140ms: still suppressed (140 < 150)
}

TEST(MenuWheelReboundTest, GenuineOppositeGestureAfterResetGapIsNotSuppressed)
{
    // After reset_gap expires, a genuine opposite gesture must proceed normally.
    BurstState s;
    EXPECT_EQ(1,  s.call(+1,   0)); // down
    EXPECT_EQ(-1, s.call(-1, 160)); // genuine opposite (delta=160 >= reset_gap=150)
}

TEST(MenuWheelReboundTest, ReboundWindowExpiresExactlyAtResetGap)
{
    // At exactly reset_gap the rebound guard must NOT suppress.
    BurstState s;
    EXPECT_EQ(1,  s.call(+1,   0));  // down
    EXPECT_EQ(-1, s.call(-1, 150));  // delta=150 == reset_gap: NOT suppressed
}

TEST(MenuWheelReboundTest, ReboundDoesNotAlterStateForSubsequentSameDirection)
{
    // After suppression the locked direction must remain valid so a continued
    // slow same-direction event still resolves to a second row (>= slow_gap).
    BurstState s;
    EXPECT_EQ(1,  s.call(+1,  0));   // down: committed, last_tick=0
    EXPECT_EQ(0,  s.call(-1, 50));   // rebound: suppressed, last_tick stays 0
    EXPECT_EQ(1,  s.call(+1, 100));  // second slow notch (delta from t=0 is 100 >= 75)
}

TEST(MenuWheelReboundTest, FastBurstDirectionChangeIsNotSuppressed)
{
    // After entering ACCELERATED mode a direction change must not be suppressed
    // even if it arrives quickly — fast bursts must remain fully responsive.
    BurstState s;
    EXPECT_EQ(1, s.call(+5,  0));  // first: commit +1, PRECISE, last_tick=0
    EXPECT_EQ(0, s.call(+5, 10));  // fast same-dir: enters ACCELERATED
    EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_ACCELERATED, s.mode);
    // Opposite direction while accelerated: guard does NOT apply → emits immediately.
    EXPECT_EQ(-1, s.call(-1, 15));
    EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE, s.mode);
}

TEST(MenuWheelReboundTest, NewDirectionAfterResetGapPlusPauseIsNotSuppressed)
{
    // After reset_gap a new opposite gesture with sufficient gap still works.
    BurstState s;
    EXPECT_EQ(1,  s.call(+1,   0));   // down
    EXPECT_EQ(1,  s.call(+1, 160));   // same dir after reset_gap: new precise, last_tick=160
    EXPECT_EQ(-1, s.call(-1, 320));   // delta=160 >= reset_gap=150: genuine opposite
}

TEST(MenuWheelReboundTest, ReboundAfterResetGapStillSuppressed)
{
    // After a long-pause reset the new gesture still gets a lock window.
    BurstState s;
    EXPECT_EQ(1,  s.call(+1,   0));   // first down
    EXPECT_EQ(1,  s.call(+1, 160));   // second down after reset_gap: emits, last_tick=160
    EXPECT_EQ(0,  s.call(-1, 190));   // rebound 30ms after second: suppressed (30 < 150)
    EXPECT_EQ(-1, s.call(-1, 320));   // genuine opposite 160ms after second: not suppressed
}
