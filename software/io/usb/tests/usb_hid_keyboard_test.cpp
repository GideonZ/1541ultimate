#include "host_test/host_test.h"
#include <string.h>
#include "../hid_decoder.h"

namespace {

const uint8_t kKeyboardDescriptor[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
	0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
	0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08,
	0x15, 0x00, 0x25, 0x65, 0x19, 0x00, 0x29, 0x65,
	0x81, 0x00, 0xC0,
};

const uint8_t kNkroKeyboardDescriptor[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
	0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
	0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x19, 0x00, 0x29, 0x65, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x96, 0x66, 0x00, 0x81, 0x02, 0xC0,
};

} // namespace

TEST(HidKeyboardDescriptorTest, ParseStandardKeyboardDescriptor)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_keyboard_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kKeyboardDescriptor, sizeof(kKeyboardDescriptor)));
	ASSERT_TRUE(items.locateKeyboardFields(fields));
	EXPECT_TRUE(fields.has_key_array);
	EXPECT_FALSE(fields.has_bitmap_keys);

	const uint8_t report[] = { 0x02, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00 };
	uint8_t boot_report[8];
	ASSERT_TRUE(items.synthesizeBootKeyboardReport(report, sizeof(report), boot_report));
	EXPECT_EQ(0, memcmp(report, boot_report, sizeof(report)));
}

TEST(HidKeyboardDescriptorTest, ParseNkroKeyboardDescriptor)
{
	HidReportParser parser;
	HidItemList items;
	t_hid_keyboard_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kNkroKeyboardDescriptor, sizeof(kNkroKeyboardDescriptor)));
	ASSERT_TRUE(items.locateKeyboardFields(fields));
	EXPECT_FALSE(fields.has_key_array);
	EXPECT_TRUE(fields.has_bitmap_keys);

	uint8_t report[15] = { 0 };
	report[0] = 0x01;
	report[1] = 0x02;
	report[2] = 0x10;
	report[7] = 0x01;

	uint8_t boot_report[8];
	ASSERT_TRUE(items.synthesizeBootKeyboardReport(report, sizeof(report), boot_report));
	EXPECT_EQ(0x02, boot_report[0]);
	EXPECT_EQ(0x04, boot_report[2]);
	EXPECT_EQ(0x28, boot_report[3]);

	uint8_t wrong_report[15] = { 0 };
	wrong_report[0] = 0x02;
	EXPECT_FALSE(items.synthesizeBootKeyboardReport(wrong_report, sizeof(wrong_report), boot_report));
}

TEST(HidKeyboardDescriptorTest, RejectsKeyboardDescriptorMissingKeys)
{
	const uint8_t kModifiersOnlyKeyboardDescriptor[] = {
		0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
		0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
		0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0,
	};

	HidReportParser parser;
	HidItemList items;
	t_hid_keyboard_fields fields;
	ASSERT_EQ(0, parser.decode(&items, kModifiersOnlyKeyboardDescriptor, sizeof(kModifiersOnlyKeyboardDescriptor)));
	EXPECT_FALSE(items.locateKeyboardFields(fields));
	EXPECT_FALSE(fields.valid);
}

TEST(HidBootProtocolTest, CopiesBootKeyboardReport)
{
	const uint8_t report[8] = { 0x01, 0x00, 0x04, 0x28, 0x00, 0x00, 0x00, 0x00 };
	uint8_t copied[8];
	HidBootProtocol::copyKeyboardReport(report, sizeof(report), copied);
	EXPECT_EQ(0, memcmp(report, copied, sizeof(report)));
}

TEST(HidBootProtocolTest, PadsShortBootKeyboardReport)
{
	const uint8_t report[3] = { 0x02, 0x00, 0x04 };
	uint8_t copied[8];
	HidBootProtocol::copyKeyboardReport(report, sizeof(report), copied);
	EXPECT_EQ(0x02, copied[0]);
	EXPECT_EQ(0x00, copied[1]);
	EXPECT_EQ(0x04, copied[2]);
	EXPECT_EQ(0x00, copied[3]);
	EXPECT_EQ(0x00, copied[7]);
}

// Composite descriptor: keyboard (report ID 1) + mouse (report ID 2) in one interface
namespace {
const uint8_t kCompositeKeyboardMouseDescriptor[] = {
	// Keyboard collection (report ID 1)
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
	0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
	0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
	0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
	// Mouse collection (report ID 2)
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01,
	0xA1, 0x00, 0x85, 0x02, 0x05, 0x09, 0x19, 0x01,
	0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
	0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
	0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
	0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02,
	0x81, 0x06, 0xC0, 0xC0,
};
} // namespace

TEST(HidCompositeTest, DetectsBothKeyboardAndMouse)
{
	HidReportParser parser;
	HidItemList items;
	ASSERT_EQ(0, parser.decode(&items, kCompositeKeyboardMouseDescriptor, sizeof(kCompositeKeyboardMouseDescriptor)));

	t_hid_keyboard_fields kb;
	ASSERT_TRUE(items.locateKeyboardFields(kb));
	EXPECT_TRUE(kb.has_key_array);

	t_hid_mouse_fields mouse;
	ASSERT_TRUE(items.locateMouseFields(mouse));
	EXPECT_TRUE(mouse.has_button1);

	EXPECT_EQ(1, kb.key_array.report);
	EXPECT_EQ(2, mouse.mouse_x.report);

	// Keyboard report should not match mouse report ID and vice versa
	const uint8_t kb_report[] = { 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t boot_kb[8];
	ASSERT_TRUE(items.synthesizeBootKeyboardReport(kb_report, sizeof(kb_report), boot_kb));
	EXPECT_EQ(0x02, boot_kb[0]);
	EXPECT_EQ(0x04, boot_kb[2]);

	const uint8_t mouse_report[] = { 0x02, 0x01, 0x05, 0xFB };
	EXPECT_FALSE(items.synthesizeBootKeyboardReport(mouse_report, sizeof(mouse_report), boot_kb));
	EXPECT_EQ(5, HidReport::getValueFromData(mouse_report, sizeof(mouse_report), mouse.mouse_x));
	EXPECT_EQ(-5, HidReport::getValueFromData(mouse_report, sizeof(mouse_report), mouse.mouse_y));
}