#include "host_test/host_test.h"
#include "../usb_hid_selection.h"

namespace {

t_usb_hid_interface_capabilities makeCaps(bool descriptor_keyboard, bool descriptor_mouse,
	bool boot_keyboard, bool boot_mouse)
{
	t_usb_hid_interface_capabilities caps = {
		descriptor_keyboard,
		descriptor_mouse,
		boot_keyboard,
		boot_mouse,
	};
	return caps;
}

} // namespace

TEST(HidSelectionTest, SuppressesBootKeyboardWhenActiveReportKeyboardSiblingExists)
{
	t_usb_hid_interface_selection selection = usb_hid_select_interface(
		makeCaps(false, false, true, false), true, false);
	EXPECT_FALSE(selection.keyboard);
	EXPECT_FALSE(selection.mouse);
	EXPECT_FALSE(selection.use_report_protocol);
}

TEST(HidSelectionTest, KeepsReportDrivenCompositeInterfaceActive)
{
	t_usb_hid_interface_selection selection = usb_hid_select_interface(
		makeCaps(true, true, true, false), false, false);
	EXPECT_TRUE(selection.keyboard);
	EXPECT_TRUE(selection.mouse);
	EXPECT_TRUE(selection.use_report_protocol);
}

TEST(HidSelectionTest, FallsBackToBootKeyboardWhenNoReportSiblingExists)
{
	t_usb_hid_interface_selection selection = usb_hid_select_interface(
		makeCaps(false, false, true, false), false, false);
	EXPECT_TRUE(selection.keyboard);
	EXPECT_FALSE(selection.mouse);
	EXPECT_FALSE(selection.use_report_protocol);
}

TEST(HidSelectionTest, ActiveReportMouseSiblingDoesNotSuppressBootKeyboard)
{
	t_usb_hid_interface_selection selection = usb_hid_select_interface(
		makeCaps(false, false, true, false), false, true);
	EXPECT_TRUE(selection.keyboard);
	EXPECT_FALSE(selection.mouse);
	EXPECT_FALSE(selection.use_report_protocol);
}

TEST(HidSelectionTest, SuppressesBootMouseWhenActiveReportMouseSiblingExists)
{
	t_usb_hid_interface_selection selection = usb_hid_select_interface(
		makeCaps(false, false, false, true), false, true);
	EXPECT_FALSE(selection.keyboard);
	EXPECT_FALSE(selection.mouse);
	EXPECT_FALSE(selection.use_report_protocol);
}

TEST(HidSelectionTest, ActiveReportKeyboardSiblingDoesNotSuppressBootMouse)
{
	t_usb_hid_interface_selection selection = usb_hid_select_interface(
		makeCaps(false, false, false, true), true, false);
	EXPECT_FALSE(selection.keyboard);
	EXPECT_TRUE(selection.mouse);
	EXPECT_FALSE(selection.use_report_protocol);
}
