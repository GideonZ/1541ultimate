#include "../../io/usb/tests/host_test/host_test.h"
#include "../../io/usb/keyboard_usb.h"
#include "../../io/c64/joystick_output.h"
#include "../input_api.h"

namespace {

const InputKeyboardMapEntry *find_keyboard_entry(const char *name)
{
    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
    for (int i = 0; i < input_api_keyboard_map_count(); i++) {
        if (strcmp(keyboard_map[i].name, name) == 0) {
            return &keyboard_map[i];
        }
    }
    return 0;
}

bool key_active(const uint8_t matrix[8], const char *name)
{
    const InputKeyboardMapEntry *entry = find_keyboard_entry(name);
    if (!entry) {
        return false;
    }
    return (matrix[entry->row] & (1 << entry->col)) != 0;
}

void reset_joystick_output(void)
{
    JoystickOutput::instance().setUsbPort1(0x1F);
    JoystickOutput::instance().releaseAllRest();
}

} // namespace

TEST(RestKeyboardStateTest, TapOverlayAutoReleasesWithoutClearingPersistentKey)
{
    Keyboard_USB keyboard;
    uint8_t matrix[8];
    bool restore = false;

    const InputKeyboardMapEntry *shift = find_keyboard_entry("left_shift");
    const InputKeyboardMapEntry *a = find_keyboard_entry("a");
    ASSERT_TRUE(shift != 0);
    ASSERT_TRUE(a != 0);

    keyboard.restPress(shift->row, shift->col);
    keyboard.restTap(a->row, a->col, 1);
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "left_shift"));
    EXPECT_TRUE(key_active(matrix, "a"));
    EXPECT_FALSE(restore);

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));
    EXPECT_FALSE(restore);
}

TEST(RestKeyboardStateTest, ReleaseDoesNotCancelTapOverlay)
{
    Keyboard_USB keyboard;
    uint8_t matrix[8];
    bool restore = false;

    const InputKeyboardMapEntry *a = find_keyboard_entry("a");
    ASSERT_TRUE(a != 0);

    keyboard.restTap(a->row, a->col, 1);
    keyboard.restRelease(a->row, a->col);
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "a"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(key_active(matrix, "a"));
}

TEST(RestKeyboardStateTest, RestoreTapIsTemporaryAndNotPersistent)
{
    Keyboard_USB keyboard;
    uint8_t matrix[8];
    bool restore = false;
    bool persistent_restore = true;

    keyboard.restTapRestore(1);
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(restore);
    keyboard.restPersistentSnapshot(matrix, persistent_restore);
    EXPECT_FALSE(persistent_restore);

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(restore);
}

TEST(RestKeyboardStateTest, ReleaseAllClearsPersistentAndOverlayState)
{
    Keyboard_USB keyboard;
    uint8_t matrix[8];
    bool restore = false;

    const InputKeyboardMapEntry *shift = find_keyboard_entry("left_shift");
    const InputKeyboardMapEntry *a = find_keyboard_entry("a");
    ASSERT_TRUE(shift != 0);
    ASSERT_TRUE(a != 0);

    keyboard.restPress(shift->row, shift->col);
    keyboard.restTap(a->row, a->col, 2);
    keyboard.restPressRestore();
    keyboard.restReleaseAll();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));
    EXPECT_FALSE(restore);
}

TEST(RestJoystickStateTest, TapOverlayAutoReleasesWithoutClearingPersistentInput)
{
    reset_joystick_output();
    uint8_t hold[5] = { 0, 0, 0, 0, 1 };
    uint8_t port1 = 0;
    uint8_t port2 = 0;

    JoystickOutput::instance().setRestPort2Persistent(0x1E);
    JoystickOutput::instance().armRestPort2Overlay(0x0F, hold);
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x1F, port1);
    EXPECT_EQ(0x0E, port2);

    JoystickOutput::instance().tickOverlays();
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x1E, port2);
}

TEST(RestJoystickStateTest, ReleaseAllClearsBothPorts)
{
    reset_joystick_output();
    uint8_t hold[5] = { 1, 0, 0, 0, 0 };
    uint8_t port1 = 0;
    uint8_t port2 = 0;

    JoystickOutput::instance().setRestPort1Persistent(0x0F);
    JoystickOutput::instance().setRestPort2Persistent(0x1E);
    JoystickOutput::instance().armRestPort1Overlay(0x1E, hold);
    JoystickOutput::instance().releaseAllRest();
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x1F, port1);
    EXPECT_EQ(0x1F, port2);
}
