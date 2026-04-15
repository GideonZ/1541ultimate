#include "host_test/host_test.h"
#include "../hid_decoder.h"

namespace {

const uint32_t kMenuFastGapTicks = 25;
const uint32_t kMenuSlowGapTicks = 75;
const uint32_t kMenuResetGapTicks = 210;
const int kMenuBurstExtraThreshold = 15;

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
	int axis_horizontal_remainder = 0;
	int axis_vertical_remainder = 0;
	int key_horizontal_remainder = 0;
	int key_vertical_remainder = 0;
	HidMouseInterpreter::applyRelativeMotion(mouse_x, mouse_y, 3, -4);
	EXPECT_EQ(13, mouse_x);
	EXPECT_EQ(24, mouse_y);

	EXPECT_EQ(-32, HidMouseInterpreter::scaleHorizontalWheel(-2, 8, axis_horizontal_remainder));
	EXPECT_EQ(0, axis_horizontal_remainder);
	EXPECT_EQ(2, HidMouseInterpreter::scaleVerticalWheel(1, 8, axis_vertical_remainder));
	EXPECT_EQ(16, axis_vertical_remainder);
	HidMouseInterpreter::applyWheelAxisDeltas(mouse_x, mouse_y, -32, 2);
	EXPECT_EQ(-19, mouse_x);
	EXPECT_EQ(22, mouse_y);

	EXPECT_EQ(12, HidMouseInterpreter::scaleHorizontalWheelKeys(1, 8, key_horizontal_remainder));
	EXPECT_EQ(0, key_horizontal_remainder);
	EXPECT_EQ(-12, HidMouseInterpreter::scaleHorizontalWheelKeys(-1, 8, key_horizontal_remainder));
	EXPECT_EQ(0, key_horizontal_remainder);
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
	EXPECT_EQ(8, HidMouseInterpreter::normalizeVerticalWheel(1));
	EXPECT_EQ(-8, HidMouseInterpreter::normalizeVerticalWheel(-1));
	EXPECT_EQ(3, HidMouseInterpreter::applyWheelDirection(3, false));
	EXPECT_EQ(-3, HidMouseInterpreter::applyWheelDirection(3, true));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelKeys(2));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelKeys(-2));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelKeys(120));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelKeys(-120));
}

TEST(HidMouseInterpreterTest, MouseModeRoutingIncludesNativeWheelMode)
{
	EXPECT_TRUE(HidMouseInterpreter::mouseModeRoutesMotionToCursor(HidMouseInterpreter::MOUSE_MODE_CURSOR));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesMotionToCursor(HidMouseInterpreter::MOUSE_MODE_MOUSE));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesMotionToCursor(HidMouseInterpreter::MOUSE_MODE_MOUSE_CURSOR));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesMotionToCursor(HidMouseInterpreter::MOUSE_MODE_MOUSE_WHEEL));

	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesWheelToPointer(HidMouseInterpreter::MOUSE_MODE_CURSOR));
	EXPECT_TRUE(HidMouseInterpreter::mouseModeRoutesWheelToPointer(HidMouseInterpreter::MOUSE_MODE_MOUSE));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesWheelToPointer(HidMouseInterpreter::MOUSE_MODE_MOUSE_CURSOR));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesWheelToPointer(HidMouseInterpreter::MOUSE_MODE_MOUSE_WHEEL));

	EXPECT_TRUE(HidMouseInterpreter::mouseModeRoutesWheelToCursor(HidMouseInterpreter::MOUSE_MODE_CURSOR));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesWheelToCursor(HidMouseInterpreter::MOUSE_MODE_MOUSE));
	EXPECT_TRUE(HidMouseInterpreter::mouseModeRoutesWheelToCursor(HidMouseInterpreter::MOUSE_MODE_MOUSE_CURSOR));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesWheelToCursor(HidMouseInterpreter::MOUSE_MODE_MOUSE_WHEEL));

	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesWheelToNative(HidMouseInterpreter::MOUSE_MODE_CURSOR));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesWheelToNative(HidMouseInterpreter::MOUSE_MODE_MOUSE));
	EXPECT_FALSE(HidMouseInterpreter::mouseModeRoutesWheelToNative(HidMouseInterpreter::MOUSE_MODE_MOUSE_CURSOR));
	EXPECT_TRUE(HidMouseInterpreter::mouseModeRoutesWheelToNative(HidMouseInterpreter::MOUSE_MODE_MOUSE_WHEEL));
}

TEST(HidMouseInterpreterTest, WheelStepAccumulationPreservesRemainder)
{
	int accumulator = 0;

	EXPECT_EQ(0, HidMouseInterpreter::accumulateNativeWheelSteps(1, 4, accumulator));
	EXPECT_EQ(4, accumulator);
	EXPECT_EQ(1, HidMouseInterpreter::accumulateNativeWheelSteps(1, 4, accumulator));
	EXPECT_EQ(0, accumulator);
	EXPECT_EQ(0, HidMouseInterpreter::accumulateNativeWheelSteps(1, 4, accumulator));
	EXPECT_EQ(4, accumulator);
	EXPECT_EQ(-1, HidMouseInterpreter::accumulateNativeWheelSteps(-3, 4, accumulator));
	EXPECT_EQ(-4, accumulator);
}

TEST(HidMouseInterpreterTest, WheelStepAccumulationSupportsMultipleQueuedSteps)
{
	int accumulator = 0;

	EXPECT_EQ(2, HidMouseInterpreter::accumulateNativeWheelSteps(17, 1, accumulator));
	EXPECT_EQ(1, accumulator);
	EXPECT_EQ(-2, HidMouseInterpreter::accumulateNativeWheelSteps(-17, 1, accumulator));
	EXPECT_EQ(-1, accumulator);

	EXPECT_EQ(16, HidMouseInterpreter::accumulateNativeWheelSteps(8, 16, accumulator));
	EXPECT_EQ(0, accumulator);
}

TEST(HidMouseInterpreterTest, NativeWheelThresholdStaysCanonicalDetentSize)
{
	EXPECT_EQ(8, HidMouseInterpreter::computeNativeWheelThreshold(1));
	EXPECT_EQ(8, HidMouseInterpreter::computeNativeWheelThreshold(4));
	EXPECT_EQ(8, HidMouseInterpreter::computeNativeWheelThreshold(8));
	EXPECT_EQ(8, HidMouseInterpreter::computeNativeWheelThreshold(16));
}

TEST(HidMouseInterpreterTest, HigherNativeWheelSensitivityProducesMoreSteps)
{
	int slow_accumulator = 0;
	int medium_accumulator = 0;
	int fast_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::accumulateNativeWheelSteps(8, 1, slow_accumulator));
	EXPECT_EQ(8, HidMouseInterpreter::accumulateNativeWheelSteps(8, 8, medium_accumulator));
	EXPECT_EQ(16, HidMouseInterpreter::accumulateNativeWheelSteps(8, 16, fast_accumulator));
	EXPECT_EQ(0, slow_accumulator);
	EXPECT_EQ(0, medium_accumulator);
	EXPECT_EQ(0, fast_accumulator);
}

TEST(HidMouseInterpreterTest, NativeWheelLowSensitivityKeepsSingleDetentLinear)
{
	int accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::accumulateNativeWheelSteps(8, 1, accumulator));
	EXPECT_EQ(0, accumulator);
}

TEST(HidMouseInterpreterTest, SingleNormalizedDetentAlwaysProducesAtLeastOneNativeStep)
{
	for (int sensitivity = 1; sensitivity <= 16; sensitivity++) {
		int accumulator = 0;
		EXPECT_TRUE(HidMouseInterpreter::accumulateNativeWheelSteps(8, sensitivity, accumulator) > 0);
	}
}

TEST(HidMouseInterpreterTest, NativeWheelAccumulatorDropsOppositeRemainder)
{
	int accumulator = 5;

	EXPECT_EQ(-1, HidMouseInterpreter::accumulateNativeWheelSteps(-8, 1, accumulator));
	EXPECT_EQ(0, accumulator);
}

TEST(HidMouseInterpreterTest, NativeWheelBurstUsesLatestDirection)
{
	int burst_direction = 0;
	uint8_t burst_count = 0;
	int phase = HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE;
	uint8_t pulse_mask = 0;
	uint32_t next_tick = 0;

	HidMouseInterpreter::mergeNativeWheelBurst(4, 4, burst_direction, burst_count);
	EXPECT_EQ(1, burst_direction);
	EXPECT_EQ(4, burst_count);

	HidMouseInterpreter::mergeNativeWheelBurst(-2, 4, burst_direction, burst_count);
	EXPECT_EQ(-1, burst_direction);
	EXPECT_EQ(2, burst_count);

	EXPECT_TRUE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 0, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_LOW, phase);
	EXPECT_EQ(0x08, pulse_mask);
	EXPECT_EQ(1, burst_count);

	EXPECT_TRUE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 10, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_HIGH, phase);
	EXPECT_TRUE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 20, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_LOW, phase);
	EXPECT_EQ(0x08, pulse_mask);
	EXPECT_EQ(0, burst_count);
}

TEST(HidMouseInterpreterTest, NativeWheelBurstSaturatesInsteadOfGrowingLatency)
{
	int burst_direction = 0;
	uint8_t burst_count = 0;

	HidMouseInterpreter::mergeNativeWheelBurst(8, 4, burst_direction, burst_count);
	EXPECT_EQ(1, burst_direction);
	EXPECT_EQ(4, burst_count);
}

TEST(HidMouseInterpreterTest, WheelPulseSequenceProducesDistinctLowAndHighPhases)
{
	int burst_direction = 1;
	uint8_t burst_count = 2;
	int phase = HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE;
	uint8_t pulse_mask = 0;
	uint32_t next_tick = 0;

	EXPECT_TRUE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 0, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_LOW, phase);
	EXPECT_EQ(0x04, pulse_mask);
	EXPECT_EQ(1, burst_count);
	EXPECT_EQ(0x1B, HidMouseInterpreter::applyWheelPulseMask(0x1F, phase, pulse_mask));

	EXPECT_TRUE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 10, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_HIGH, phase);
	EXPECT_EQ(0x1F, HidMouseInterpreter::applyWheelPulseMask(0x1F, phase, pulse_mask));

	EXPECT_TRUE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 20, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_LOW, phase);
	EXPECT_EQ(0x04, pulse_mask);
	EXPECT_EQ(0, burst_count);

	EXPECT_TRUE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 30, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_HIGH, phase);

	EXPECT_FALSE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 40, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE, phase);
	EXPECT_EQ(0, burst_direction);
	EXPECT_EQ(0, pulse_mask);
	EXPECT_EQ(0x1F, HidMouseInterpreter::applyWheelPulseMask(0x1F, phase, pulse_mask));
}

TEST(HidMouseInterpreterTest, WheelPulseDirectionSelectsDownBit)
{
	int burst_direction = -1;
	uint8_t burst_count = 1;
	int phase = HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE;
	uint8_t pulse_mask = 0;
	uint32_t next_tick = 0;

	EXPECT_TRUE(HidMouseInterpreter::advanceNativeWheelBurst(burst_direction, burst_count, phase, pulse_mask, next_tick, 0, 10));
	EXPECT_EQ(HidMouseInterpreter::WHEEL_PULSE_PHASE_LOW, phase);
	EXPECT_EQ(0x08, pulse_mask);
	EXPECT_EQ(0x17, HidMouseInterpreter::applyWheelPulseMask(0x1F, phase, pulse_mask));
}

TEST(HidMouseInterpreterTest, SingleDetentVerticalWheelNormalizesToUsableStep)
{
	int axis_remainder = 0;
	int key_remainder = 0;

	EXPECT_EQ(20, HidMouseInterpreter::scaleVerticalWheel(
		HidMouseInterpreter::normalizeVerticalWheel(1), 8, axis_remainder));
	EXPECT_EQ(0, axis_remainder);
	EXPECT_EQ(16, HidMouseInterpreter::scaleVerticalWheelKeys(
		HidMouseInterpreter::normalizeVerticalWheel(1), 8, key_remainder));
	EXPECT_EQ(0, key_remainder);

	EXPECT_EQ(-20, HidMouseInterpreter::scaleVerticalWheel(
		HidMouseInterpreter::normalizeVerticalWheel(-1), 8, axis_remainder));
	EXPECT_EQ(0, axis_remainder);
	EXPECT_EQ(-16, HidMouseInterpreter::scaleVerticalWheelKeys(
		HidMouseInterpreter::normalizeVerticalWheel(-1), 8, key_remainder));
	EXPECT_EQ(0, key_remainder);
}

TEST(HidMouseInterpreterTest, SlowMenuWheelBurstLocksUntilReset)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 0, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 40, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 120, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 340, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
}

TEST(HidMouseInterpreterTest, SeparatedSlowMenuWheelNotchesStartNewBursts)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 0, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 220, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 440, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
}

TEST(HidMouseInterpreterTest, FastMenuWheelBurstAcceleratesModerately)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-1, 0, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(-5, 10, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(-5, 20, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-5, 30, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(-5, 90, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(-5, 100, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-5, 110, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
}

TEST(HidMouseInterpreterTest, HighResolutionSlowWheelDoesNotLeakPastOneStep)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 0, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 10, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 20, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 30, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(1, 120, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 340, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
}

TEST(HidMouseInterpreterTest, MenuWheelResetAndDirectionChangesReturnToPreciseMode)
{
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	EXPECT_EQ(1, HidMouseInterpreter::scaleMenuWheelBurst(1, 0, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(0, HidMouseInterpreter::scaleMenuWheelBurst(5, 10, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_ACCELERATED, mode);
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-1, 20, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
	EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE, mode);
	EXPECT_EQ(-1, HidMouseInterpreter::scaleMenuWheelBurst(-1, 240, last_tick, mode, burst_direction, burst_accumulator, kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold));
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

namespace {
struct BurstState {
	uint32_t last_tick = 0;
	int mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
	int burst_direction = 0;
	int burst_accumulator = 0;

	int call(int delta, uint32_t now)
	{
		return HidMouseInterpreter::scaleMenuWheelBurst(
			delta, now, last_tick, mode, burst_direction, burst_accumulator,
			kMenuFastGapTicks, kMenuSlowGapTicks, kMenuResetGapTicks, kMenuBurstExtraThreshold);
	}
};
} // namespace

TEST(MenuWheelReboundTest, SlowDownwardReboundIsSuppressed)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(0, s.call(-1, 50));
	EXPECT_EQ(0, s.call(-1, 60));
}

TEST(MenuWheelReboundTest, SlowUpwardReboundIsSuppressed)
{
	BurstState s;
	EXPECT_EQ(-1, s.call(-1, 0));
	EXPECT_EQ(0, s.call(+1, 50));
}

TEST(MenuWheelReboundTest, LateReboundBeyondSlowGapStillSuppressedByResetGap)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(0, s.call(-1, 80));
	EXPECT_EQ(0, s.call(-1, 100));
	EXPECT_EQ(0, s.call(-1, 140));
}

TEST(MenuWheelReboundTest, GenuineOppositeGestureAfterResetGapIsNotSuppressed)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(-1, s.call(-1, 220));
}

TEST(MenuWheelReboundTest, ReboundWindowExpiresExactlyAtResetGap)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(-1, s.call(-1, kMenuResetGapTicks));
}

TEST(MenuWheelReboundTest, ReboundDoesNotAlterStateForSubsequentSameDirection)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(0, s.call(-1, 50));
	EXPECT_EQ(0, s.call(+1, 100));
	EXPECT_EQ(1, s.call(+1, 320));
}

TEST(MenuWheelReboundTest, FastBurstDirectionChangeIsNotSuppressed)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+5, 0));
	EXPECT_EQ(0, s.call(+5, 10));
	EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_ACCELERATED, s.mode);
	EXPECT_EQ(-1, s.call(-1, 15));
	EXPECT_EQ(HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE, s.mode);
}

TEST(MenuWheelReboundTest, NewDirectionAfterResetGapPlusPauseIsNotSuppressed)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(1, s.call(+1, 220));
	EXPECT_EQ(-1, s.call(-1, 440));
}

TEST(MenuWheelReboundTest, ReboundAfterResetGapStillSuppressed)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(1, s.call(+1, 220));
	EXPECT_EQ(0, s.call(-1, 250));
	EXPECT_EQ(0, s.call(-1, 380));
	EXPECT_EQ(-1, s.call(-1, 530));
}

TEST(MenuWheelReboundTest, SameDirectionReportsStayCollapsedUntilQuietReset)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(0, s.call(+1, 80));
	EXPECT_EQ(0, s.call(+1, 140));
	EXPECT_EQ(1, s.call(+1, 360));
}

TEST(MenuWheelReboundTest, OppositeCleanupDoesNotExtendResetPastLastPrimary)
{
	BurstState s;
	EXPECT_EQ(1, s.call(+1, 0));
	EXPECT_EQ(0, s.call(+1, 90));
	EXPECT_EQ(0, s.call(-1, 200));
	EXPECT_EQ(-1, s.call(-1, 320));
}

TEST(SensitivityScalingTest, IdentityAtDefaultSensitivity)
{
	EXPECT_EQ(1, HidMouseInterpreter::scaleSensitivity(1, 8));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleSensitivity(-1, 8));
	EXPECT_EQ(127, HidMouseInterpreter::scaleSensitivity(127, 8));
	EXPECT_EQ(-127, HidMouseInterpreter::scaleSensitivity(-127, 8));
	EXPECT_EQ(0, HidMouseInterpreter::scaleSensitivity(0, 8));
}

TEST(SensitivityScalingTest, MaxSensitivityDoublesOutput)
{
	EXPECT_EQ(2, HidMouseInterpreter::scaleSensitivity(1, 16));
	EXPECT_EQ(-2, HidMouseInterpreter::scaleSensitivity(-1, 16));
	EXPECT_EQ(254, HidMouseInterpreter::scaleSensitivity(127, 16));
	EXPECT_EQ(-254, HidMouseInterpreter::scaleSensitivity(-127, 16));
}

TEST(SensitivityScalingTest, MinSensitivityAbsorbsSmallDeltas)
{
	EXPECT_EQ(0, HidMouseInterpreter::scaleSensitivity(1, 1));
	EXPECT_EQ(0, HidMouseInterpreter::scaleSensitivity(-1, 1));
	EXPECT_EQ(0, HidMouseInterpreter::scaleSensitivity(7, 1));
	EXPECT_EQ(-1, HidMouseInterpreter::scaleSensitivity(-8, 1));
	EXPECT_EQ(15, HidMouseInterpreter::scaleSensitivity(127, 1));
}

TEST(SensitivityScalingTest, HalfSensitivityHalvesOutput)
{
	EXPECT_EQ(0, HidMouseInterpreter::scaleSensitivity(1, 4));
	EXPECT_EQ(1, HidMouseInterpreter::scaleSensitivity(2, 4));
	EXPECT_EQ(63, HidMouseInterpreter::scaleSensitivity(127, 4));
}

TEST(SensitivityScalingTest, ClampsSensitivityOutOfRange)
{
	EXPECT_EQ(1, HidMouseInterpreter::clampSensitivity(0));
	EXPECT_EQ(1, HidMouseInterpreter::clampSensitivity(-5));
	EXPECT_EQ(16, HidMouseInterpreter::clampSensitivity(20));
	EXPECT_EQ(8, HidMouseInterpreter::clampSensitivity(8));
}

TEST(DeltaClampingTest, ClampsPositiveDelta)
{
	EXPECT_EQ(63, HidMouseInterpreter::clampDelta(100, 63));
	EXPECT_EQ(63, HidMouseInterpreter::clampDelta(127, 63));
	EXPECT_EQ(63, HidMouseInterpreter::clampDelta(254, 63));
	EXPECT_EQ(63, HidMouseInterpreter::clampDelta(63, 63));
}

TEST(DeltaClampingTest, ClampsNegativeDelta)
{
	EXPECT_EQ(-63, HidMouseInterpreter::clampDelta(-100, 63));
	EXPECT_EQ(-63, HidMouseInterpreter::clampDelta(-127, 63));
	EXPECT_EQ(-63, HidMouseInterpreter::clampDelta(-254, 63));
	EXPECT_EQ(-63, HidMouseInterpreter::clampDelta(-63, 63));
}

TEST(DeltaClampingTest, PassesThroughSmallDeltas)
{
	EXPECT_EQ(0, HidMouseInterpreter::clampDelta(0, 63));
	EXPECT_EQ(1, HidMouseInterpreter::clampDelta(1, 63));
	EXPECT_EQ(-1, HidMouseInterpreter::clampDelta(-1, 63));
	EXPECT_EQ(50, HidMouseInterpreter::clampDelta(50, 63));
	EXPECT_EQ(-50, HidMouseInterpreter::clampDelta(-50, 63));
	EXPECT_EQ(62, HidMouseInterpreter::clampDelta(62, 63));
}

TEST(SensitivityClampIntegrationTest, MaxInputMaxSensitivityClampedTo63)
{
	int scaled = HidMouseInterpreter::scaleSensitivity(127, 16);
	EXPECT_EQ(254, scaled);
	EXPECT_EQ(63, HidMouseInterpreter::clampDelta(scaled, 63));
}

TEST(SensitivityClampIntegrationTest, DefaultSensitivityLargeInputClamped)
{
	int scaled = HidMouseInterpreter::scaleSensitivity(100, 8);
	EXPECT_EQ(100, scaled);
	EXPECT_EQ(63, HidMouseInterpreter::clampDelta(scaled, 63));
}

TEST(SensitivityClampIntegrationTest, SmallInputPreservedAtDefaultSensitivity)
{
	int scaled = HidMouseInterpreter::scaleSensitivity(10, 8);
	EXPECT_EQ(10, scaled);
	EXPECT_EQ(10, HidMouseInterpreter::clampDelta(scaled, 63));
}

TEST(SensitivityClampIntegrationTest, MonotonicPotOutputUnderHighDelta)
{
	int16_t mouse_x = 0;
	int16_t mouse_y = 0;
	uint8_t prev_pot = mouse_x & 0x7F;

	for (int i = 0; i < 50; i++) {
		int motion = HidMouseInterpreter::clampDelta(
			HidMouseInterpreter::scaleSensitivity(127, 16), 63);
		HidMouseInterpreter::applyRelativeMotion(mouse_x, mouse_y, motion, 0);
		uint8_t pot = mouse_x & 0x7F;
		int delta = ((pot - prev_pot) + 128) % 128;
		if (delta > 63) {
			delta -= 128;
		}
		EXPECT_TRUE(delta >= 0);
		EXPECT_TRUE(delta <= 63);
		prev_pot = pot;
	}
}

TEST(AdaptiveAccelerationTest, ComputesExpectedScaleFromTrackedMagnitude)
{
	EXPECT_EQ(384, HidMouseInterpreter::computeAdaptiveAccelerationScale(16 << 4));
	EXPECT_EQ(384, HidMouseInterpreter::computeAdaptiveAccelerationScale(32 << 4));
	EXPECT_EQ(396, HidMouseInterpreter::computeAdaptiveAccelerationScale(48 << 4));
	EXPECT_EQ(432, HidMouseInterpreter::computeAdaptiveAccelerationScale(64 << 4));
	EXPECT_EQ(576, HidMouseInterpreter::computeAdaptiveAccelerationScale(96 << 4));
}

TEST(AdaptiveAccelerationTest, LimitsScaleChangesPerReport)
{
	EXPECT_EQ(416, HidMouseInterpreter::limitScaleStep(384, 576, 32));
	EXPECT_EQ(576, HidMouseInterpreter::limitScaleStep(544, 576, 32));
	EXPECT_EQ(384, HidMouseInterpreter::limitScaleStep(384, 384, 32));
}

TEST(AdaptiveAccelerationTest, SmallMovementsStayAtBaselineScale)
{
	int adaptive_accel_ema_x16 = 0;
	int adaptive_accel_scale_factor = HidMouseInterpreter::ADAPTIVE_ACCELERATION_FALLBACK_SCALE;

	for (int i = 0; i < 50; i++) {
		adaptive_accel_ema_x16 = HidMouseInterpreter::updateAdaptiveAccelerationEma(adaptive_accel_ema_x16, 2, 0);
		adaptive_accel_scale_factor = HidMouseInterpreter::limitScaleStep(
			adaptive_accel_scale_factor,
			HidMouseInterpreter::computeAdaptiveAccelerationScale(adaptive_accel_ema_x16),
			32);
	}

	EXPECT_EQ(384, adaptive_accel_scale_factor);
}

TEST(AdaptiveAccelerationTest, HighSpeedReportsSettleToSafeMonotonicOutput)
{
	int adaptive_accel_ema_x16 = 0;
	int adaptive_accel_scale_factor = HidMouseInterpreter::ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
	int16_t mouse_x = 0;
	int16_t mouse_y = 0;
	uint8_t prev_pot = mouse_x & 0x7F;

	for (int i = 0; i < 50; i++) {
		adaptive_accel_ema_x16 = HidMouseInterpreter::updateAdaptiveAccelerationEma(adaptive_accel_ema_x16, 127, 0);
		adaptive_accel_scale_factor = HidMouseInterpreter::limitScaleStep(
			adaptive_accel_scale_factor,
			HidMouseInterpreter::computeAdaptiveAccelerationScale(adaptive_accel_ema_x16),
			32);
		int motion = HidMouseInterpreter::clampDelta(
			HidMouseInterpreter::scaleFixed(127, adaptive_accel_scale_factor), 63);
		HidMouseInterpreter::applyRelativeMotion(mouse_x, mouse_y, motion, 0);
		uint8_t pot = mouse_x & 0x7F;
		int delta = ((pot - prev_pot) + 128) % 128;
		if (delta > 63) {
			delta -= 128;
		}
		EXPECT_TRUE(delta >= 0);
		EXPECT_TRUE(delta <= 63);
		prev_pot = pot;
	}

	EXPECT_EQ(576, adaptive_accel_scale_factor);
}

TEST(AdaptiveAccelerationTest, HighSpeedMovementBoostsLowSensitivitySetting)
{
	int adaptive_accel_ema_x16 = 0;
	int adaptive_accel_scale_factor = HidMouseInterpreter::ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
	int motion = 0;
	int baseline = HidMouseInterpreter::scaleSensitivity(127, 1);

	for (int i = 0; i < 50; i++) {
		adaptive_accel_ema_x16 = HidMouseInterpreter::updateAdaptiveAccelerationEma(adaptive_accel_ema_x16, 127, 0);
		adaptive_accel_scale_factor = HidMouseInterpreter::limitScaleStep(
			adaptive_accel_scale_factor,
			HidMouseInterpreter::computeAdaptiveAccelerationScale(adaptive_accel_ema_x16),
			32);
		motion = HidMouseInterpreter::clampDelta(
			HidMouseInterpreter::scaleFixed(baseline, adaptive_accel_scale_factor), 63);
	}

	EXPECT_TRUE(motion > baseline);
	EXPECT_EQ(576, adaptive_accel_scale_factor);
}
