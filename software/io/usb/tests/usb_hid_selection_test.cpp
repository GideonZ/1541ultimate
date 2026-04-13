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

const UsbDevice *makeDeviceAddress(unsigned long value)
{
	return reinterpret_cast<const UsbDevice *>(value);
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

TEST(HidVisibilitySelectionTest, CompositeReconnectWithoutActivityDoesNotOverrideExistingKeyboard)
{
	const UsbDevice *keychron = makeDeviceAddress(0x1000);
	const UsbDevice *logitech = makeDeviceAddress(0x2000);
	EXPECT_FALSE(usb_hid_should_claim_visibility(keychron, 1, logitech, 2, false));
}

TEST(HidVisibilitySelectionTest, UnclaimedRoleCanBeFilledOnConnect)
{
	const UsbDevice *logitech = makeDeviceAddress(0x2000);
	EXPECT_TRUE(usb_hid_should_claim_visibility(0, -1, logitech, 2, false));
}

TEST(HidVisibilitySelectionTest, ActiveInputMayReplaceVisibleDevice)
{
	const UsbDevice *keychron = makeDeviceAddress(0x1000);
	const UsbDevice *logitech = makeDeviceAddress(0x2000);
	EXPECT_TRUE(usb_hid_should_claim_visibility(keychron, 1, logitech, 2, true));
}

TEST(HidVisibilitySelectionTest, DisconnectMatchesOnlyTheExactVisibleSource)
{
	const UsbDevice *keychron = makeDeviceAddress(0x1000);
	const UsbDevice *logitech = makeDeviceAddress(0x2000);
	EXPECT_TRUE(usb_hid_source_matches(keychron, 1, keychron, 1));
	EXPECT_FALSE(usb_hid_source_matches(keychron, 1, keychron, 2));
	EXPECT_FALSE(usb_hid_source_matches(keychron, 1, logitech, 1));
}
