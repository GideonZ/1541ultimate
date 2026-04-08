#include "host_test/host_test.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readTextFile(const char *path)
{
	std::ifstream input(path);
	std::ostringstream content;
	content << input.rdbuf();
	return content.str();
}

void expectOrdered(const std::string& text, const std::vector<std::string>& markers)
{
	std::string::size_type pos = 0;
	for (size_t i = 0; i < markers.size(); i++) {
		std::string::size_type found = text.find(markers[i], pos);
		ASSERT_TRUE(found != std::string::npos);
		pos = found + markers[i].size();
	}
}

} // namespace

TEST(U64MouseConfigSourceTest, DefinesMouseAccelerationAsEnum)
{
	std::string config_header = readTextFile("../usb_hid_config.h");
	std::string u64_config = readTextFile("../../../u64/u64_config.cc");

	EXPECT_NE(std::string::npos, config_header.find("CFG_MOUSE_ACCELERATION"));
	EXPECT_NE(std::string::npos, u64_config.find("CFG_MOUSE_ACCELERATION"));
	EXPECT_NE(std::string::npos, u64_config.find("\"Mouse Acceleration\""));
	EXPECT_NE(std::string::npos, u64_config.find("mouse_acceleration_modes"));
	EXPECT_NE(std::string::npos, u64_config.find("CFG_TYPE_ENUM, \"Mouse Acceleration\""));
}

TEST(U64MouseConfigSourceTest, UsesMouseWheelSensitivityTerminology)
{
	std::string u64_config = readTextFile("../../../u64/u64_config.cc");

	EXPECT_NE(std::string::npos, u64_config.find("\"Mouse Wheel Sensitivity\""));
	EXPECT_EQ(std::string::npos, u64_config.find("\"Mouse Wheel Factor\""));
}

TEST(U64MouseConfigSourceTest, KeepsMouseDefinitionOrder)
{
	std::string u64_config = readTextFile("../../../u64/u64_config.cc");
	expectOrdered(u64_config, {
		"CFG_MOUSE_SENSITIVITY",
		"CFG_MOUSE_ACCELERATION",
		"CFG_WHEEL_MODE",
		"CFG_SCROLL_FACTOR",
		"CFG_WHEEL_DIRECTION",
		"CFG_MENU_MOUSE_NAV",
		"CFG_USB_MOUSE_NAME"
	});
}

TEST(U64MouseConfigSourceTest, GroupsJoystickMenuWithSeparatorsOnly)
{
	std::string u64_config = readTextFile("../../../u64/u64_config.cc");
	std::string::size_type start = u64_config.find("grp = ConfigGroupCollection :: getGroup(\"Joystick Settings\"");
	ASSERT_TRUE(start != std::string::npos);
	std::string::size_type end = u64_config.find("#if U64==2", start);
	ASSERT_TRUE(end != std::string::npos);
	std::string block = u64_config.substr(start, end - start);

	expectOrdered(block, {
		"CFG_JOYSWAP",
		"CFG_PADDLE_EN",
		"ConfigItem :: separator()",
		"CFG_MOUSE_SENSITIVITY",
		"CFG_MOUSE_ACCELERATION",
		"CFG_MENU_MOUSE_NAV",
		"ConfigItem :: separator()",
		"CFG_WHEEL_MODE",
		"CFG_SCROLL_FACTOR",
		"CFG_WHEEL_DIRECTION",
		"ConfigItem :: separator()",
		"CFG_USB_MOUSE_NAME",
		"CFG_USB_MOUSE_MODE",
		"CFG_USB_KEYBOARD_NAME",
		"CFG_USB_KEYBOARD_MODE"
	});
}

TEST(U64MouseConfigSourceTest, NewAccelerationUsesExistingEnumSerializationPath)
{
	std::string u64_config = readTextFile("../../../u64/u64_config.cc");
	std::string config_impl = readTextFile("../../../components/config.cc");

	EXPECT_NE(std::string::npos, u64_config.find("CFG_MOUSE_ACCELERATION,   CFG_TYPE_ENUM"));
	EXPECT_NE(std::string::npos, config_impl.find("case CFG_TYPE_ENUM:"));
	EXPECT_NE(std::string::npos, config_impl.find("*(buffer++) = (uint8_t)1;"));
}
