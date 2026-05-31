#include "../../io/usb/tests/host_test/host_test.h"
#include "../../io/c64/keyboard_c64.h"
#include "../../io/usb/keyboard_usb.h"
#include "../../io/c64/joystick_output.h"
#include "../input_api.h"
#include "../route_input_menu.h"

#include <initializer_list>

extern "C" void wait_ms(int)
{
}

namespace {

RouteInputMenuKeyboardState route_input_test_menu_state;

void route_input_reset_menu_keyboard_state_for_test(void)
{
    route_input_test_menu_state.reset();
}

int route_input_apply_menu_keyboard_event_for_test(const InputParsedEvent &event,
    bool matrix_enabled, uint8_t *keys, int max_keys, int initial_repeat_delay)
{
    bool menu_active = !matrix_enabled;
    route_input_test_menu_state.sync(menu_active);
    if (!menu_active) {
        return 0;
    }
    return route_input_test_menu_state.applyKeyboardEvent(event, keys, max_keys, initial_repeat_delay);
}

int route_input_menu_repeat_tick_for_test(bool matrix_enabled, uint8_t *keys, int max_keys, int repeat_speed)
{
    bool menu_active = !matrix_enabled;
    route_input_test_menu_state.sync(menu_active);
    if (!menu_active) {
        return 0;
    }
    return route_input_test_menu_state.repeatTick(keys, max_keys, repeat_speed);
}

void route_input_menu_snapshot_for_test(uint8_t matrix[8])
{
    route_input_test_menu_state.snapshot(matrix);
}

void route_input_release_all_for_test(void)
{
    route_input_test_menu_state.clear();
}

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

int find_keyboard_index(const char *name)
{
    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
    for (int i = 0; i < input_api_keyboard_map_count(); i++) {
        if (strcmp(keyboard_map[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

InputParsedEvent keyboard_event(InputParsedTransition transition, std::initializer_list<const char *> names)
{
    InputParsedEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = INPUT_PARSED_KEYBOARD;
    event.transition = transition;

    for (const char *name : names) {
        ASSERT_TRUE(event.keyboard_count < INPUT_API_MAX_KEYBOARD_INPUTS);
        int index = find_keyboard_index(name);
        ASSERT_TRUE(index >= 0);
        event.keyboard_index[event.keyboard_count++] = index;
    }
    return event;
}

int collect_menu_keys(InputParsedTransition transition, std::initializer_list<const char *> names, uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS])
{
    InputParsedEvent event = keyboard_event(transition, names);
    return RouteInputMenuKeyboardState::collectKeys(event, keys, INPUT_API_MAX_KEYBOARD_INPUTS);
}

int collect_menu_keys_with_state(InputParsedTransition transition, std::initializer_list<const char *> names, uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS], uint8_t &held_modifier_flags)
{
    InputParsedEvent event = keyboard_event(transition, names);
    return RouteInputMenuKeyboardState::collectKeys(event, keys, INPUT_API_MAX_KEYBOARD_INPUTS, &held_modifier_flags);
}

bool key_active(const uint8_t matrix[8], const char *name)
{
    const InputKeyboardMapEntry *entry = find_keyboard_entry(name);
    if (!entry) {
        return false;
    }
    return (matrix[entry->row] & (1 << entry->col)) != 0;
}

void add_key_to_matrix(uint8_t matrix[8], const char *name)
{
    const InputKeyboardMapEntry *entry = find_keyboard_entry(name);
    ASSERT_TRUE(entry != 0);
    matrix[entry->row] |= (1 << entry->col);
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

TEST(RestKeyboardStateTest, QueuedTapPreservesChordAndOrder)
{
    Keyboard_USB keyboard;
    uint8_t matrix[8];
    bool restore = false;
    uint8_t shift_a[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t b_only[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    add_key_to_matrix(shift_a, "left_shift");
    add_key_to_matrix(shift_a, "a");
    add_key_to_matrix(b_only, "b");

    ASSERT_TRUE(keyboard.restQueueTap(shift_a, false, 1));
    ASSERT_TRUE(keyboard.restQueueTap(b_only, false, 1));

    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));
    EXPECT_FALSE(key_active(matrix, "b"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "left_shift"));
    EXPECT_TRUE(key_active(matrix, "a"));
    EXPECT_FALSE(key_active(matrix, "b"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));
    EXPECT_FALSE(key_active(matrix, "b"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(key_active(matrix, "b"));
    EXPECT_FALSE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "b"));
    EXPECT_FALSE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(key_active(matrix, "b"));
    EXPECT_FALSE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));
}

TEST(RestKeyboardStateTest, QueuedTapStagesModifiersAroundChordKey)
{
    Keyboard_USB keyboard;
    uint8_t matrix[8];
    bool restore = false;
    uint8_t shift_a[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t setup[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    add_key_to_matrix(shift_a, "left_shift");
    add_key_to_matrix(shift_a, "a");
    add_key_to_matrix(setup, "left_shift");

    ASSERT_TRUE(keyboard.restQueueTap(shift_a, setup, false, 2));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "left_shift"));
    EXPECT_TRUE(key_active(matrix, "a"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "left_shift"));
    EXPECT_TRUE(key_active(matrix, "a"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_TRUE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));

    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));
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

TEST(RestKeyboardStateTest, RestMatrixOutputSurvivesUsbMatrixDisable)
{
    Keyboard_USB keyboard;
    volatile uint8_t hardware_matrix[11] = { 0 };

    const InputKeyboardMapEntry *b = find_keyboard_entry("b");
    ASSERT_TRUE(b != 0);

    keyboard.setMatrix(hardware_matrix);
    keyboard.enableMatrix(true);
    keyboard.restPress(b->row, b->col);
    EXPECT_TRUE((hardware_matrix[b->row] & (1 << b->col)) != 0);

    keyboard.enableMatrix(false);
    EXPECT_TRUE((hardware_matrix[b->row] & (1 << b->col)) != 0);

    keyboard.restPressRestore();
    EXPECT_TRUE((hardware_matrix[9] & 1) != 0);

    keyboard.restRelease(b->row, b->col);
    EXPECT_FALSE((hardware_matrix[b->row] & (1 << b->col)) != 0);

    keyboard.restReleaseRestore();
    EXPECT_FALSE((hardware_matrix[9] & 1) != 0);
}

TEST(KeyboardC64StateTest, SoftwareJoystickDoesNotBlockKeyboardScan)
{
    EXPECT_FALSE(Keyboard_C64::joystick_blocks_keyboard(0x0E, 0x0E));
    EXPECT_TRUE(Keyboard_C64::joystick_blocks_keyboard(0x0E, 0x1F));
    EXPECT_TRUE(Keyboard_C64::joystick_blocks_keyboard(0x0C, 0x0E));
}

TEST(KeyboardC64StateTest, MatrixLookupTranslatesUiKeyCodes)
{
    struct KeyCase {
        const char *name;
        uint8_t shift_flag;
        uint8_t expected_key;
    };
    const KeyCase cases[] = {
        { "cursor_up_down", 0x00, KEY_DOWN },
        { "cursor_up_down", 0x01, KEY_UP },
        { "cursor_left_right", 0x00, KEY_RIGHT },
        { "cursor_left_right", 0x01, KEY_LEFT },
        { "return", 0x00, KEY_RETURN },
        { "f1", 0x00, KEY_F1 },
        { "f1", 0x01, KEY_F2 },
        { "a", 0x00, 'a' },
        { "a", 0x01, 'A' },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const InputKeyboardMapEntry *entry = find_keyboard_entry(cases[i].name);
        ASSERT_TRUE(entry != 0);
        EXPECT_EQ(cases[i].expected_key, Keyboard_C64::matrixToKeyCode(entry->row, entry->col, cases[i].shift_flag));
    }

    const InputKeyboardMapEntry *shift = find_keyboard_entry("left_shift");
    const InputKeyboardMapEntry *commodore = find_keyboard_entry("commodore");
    const InputKeyboardMapEntry *ctrl = find_keyboard_entry("ctrl");
    ASSERT_TRUE(shift != 0);
    ASSERT_TRUE(commodore != 0);
    ASSERT_TRUE(ctrl != 0);
    EXPECT_EQ(0x01, Keyboard_C64::matrixModifierFlag(shift->row, shift->col));
    EXPECT_EQ(0x02, Keyboard_C64::matrixModifierFlag(commodore->row, commodore->col));
    EXPECT_EQ(0x04, Keyboard_C64::matrixModifierFlag(ctrl->row, ctrl->col));
    EXPECT_EQ(0x00, Keyboard_C64::matrixModifierFlag(8, 0));
    EXPECT_EQ(0x00, Keyboard_C64::matrixToKeyCode(8, 0, 0));
}

TEST(RouteInputMenuTranslationTest, CollectsMenuKeysWithModifiers)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "cursor_up_down" }, keys));
    EXPECT_EQ(KEY_DOWN, keys[0]);

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "left_shift", "cursor_up_down" }, keys));
    EXPECT_EQ(KEY_UP, keys[0]);

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "cursor_left_right" }, keys));
    EXPECT_EQ(KEY_RIGHT, keys[0]);

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "left_shift", "cursor_left_right" }, keys));
    EXPECT_EQ(KEY_LEFT, keys[0]);

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "return" }, keys));
    EXPECT_EQ(KEY_RETURN, keys[0]);

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "f1" }, keys));
    EXPECT_EQ(KEY_F1, keys[0]);

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "left_shift", "f1" }, keys));
    EXPECT_EQ(KEY_F2, keys[0]);

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "a" }, keys));
    EXPECT_EQ('a', keys[0]);

    EXPECT_EQ(1, collect_menu_keys(INPUT_PARSED_TAP, { "left_shift", "a" }, keys));
    EXPECT_EQ('A', keys[0]);
}

TEST(RouteInputMenuTranslationTest, IgnoresReleaseRestoreAndModifierOnlyEvents)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];

    EXPECT_EQ(0, collect_menu_keys(INPUT_PARSED_RELEASE, { "a" }, keys));
    EXPECT_EQ(0, collect_menu_keys(INPUT_PARSED_TAP, { "restore" }, keys));
    EXPECT_EQ(0, collect_menu_keys(INPUT_PARSED_TAP, { "left_shift" }, keys));
}

TEST(RouteInputMenuTranslationTest, PreservesNonModifierOrder)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];

    EXPECT_EQ(2, collect_menu_keys(INPUT_PARSED_PRESS, { "left_shift", "a", "cursor_left_right" }, keys));
    EXPECT_EQ('A', keys[0]);
    EXPECT_EQ(KEY_LEFT, keys[1]);
}

TEST(RouteInputMenuTranslationTest, AppliesOrderedBatchModifierState)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];
    uint8_t held_modifier_flags = 0;

    EXPECT_EQ(0, collect_menu_keys_with_state(INPUT_PARSED_PRESS, { "left_shift" }, keys, held_modifier_flags));
    EXPECT_EQ(0x01, held_modifier_flags);

    EXPECT_EQ(1, collect_menu_keys_with_state(INPUT_PARSED_PRESS, { "cursor_up_down" }, keys, held_modifier_flags));
    EXPECT_EQ(KEY_UP, keys[0]);

    EXPECT_EQ(1, collect_menu_keys_with_state(INPUT_PARSED_PRESS, { "cursor_left_right" }, keys, held_modifier_flags));
    EXPECT_EQ(KEY_LEFT, keys[0]);

    EXPECT_EQ(1, collect_menu_keys_with_state(INPUT_PARSED_PRESS, { "f1" }, keys, held_modifier_flags));
    EXPECT_EQ(KEY_F2, keys[0]);

    EXPECT_EQ(0, collect_menu_keys_with_state(INPUT_PARSED_RELEASE, { "left_shift" }, keys, held_modifier_flags));
    EXPECT_EQ(0x00, held_modifier_flags);

    EXPECT_EQ(1, collect_menu_keys_with_state(INPUT_PARSED_PRESS, { "cursor_up_down" }, keys, held_modifier_flags));
    EXPECT_EQ(KEY_DOWN, keys[0]);
}

TEST(RouteInputMenuStateTest, PersistsModifiersAcrossSeparateEvents)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];
    uint8_t held_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    route_input_reset_menu_keyboard_state_for_test();

    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "left_shift" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    route_input_menu_snapshot_for_test(held_matrix);
    EXPECT_TRUE(key_active(held_matrix, "left_shift"));

    EXPECT_EQ(1, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "a" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    EXPECT_EQ('A', keys[0]);
    route_input_menu_snapshot_for_test(held_matrix);
    EXPECT_TRUE(key_active(held_matrix, "left_shift"));
    EXPECT_TRUE(key_active(held_matrix, "a"));

    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_RELEASE, { "a" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_RELEASE, { "left_shift" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    route_input_menu_snapshot_for_test(held_matrix);
    EXPECT_FALSE(key_active(held_matrix, "left_shift"));
    EXPECT_FALSE(key_active(held_matrix, "a"));
}

TEST(RouteInputMenuStateTest, UsesHeldModifiersForTapEvents)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];
    uint8_t held_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    route_input_reset_menu_keyboard_state_for_test();

    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "left_shift" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    EXPECT_EQ(1, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_TAP, { "a" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    EXPECT_EQ('A', keys[0]);

    route_input_menu_snapshot_for_test(held_matrix);
    EXPECT_TRUE(key_active(held_matrix, "left_shift"));
    EXPECT_FALSE(key_active(held_matrix, "a"));
}

TEST(RouteInputMenuStateTest, RepeatsHeldKeysUntilReleased)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];

    route_input_reset_menu_keyboard_state_for_test();

    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "left_shift" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 2));
    EXPECT_EQ(1, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "cursor_up_down" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 2));
    EXPECT_EQ(KEY_UP, keys[0]);

    EXPECT_EQ(0, route_input_menu_repeat_tick_for_test(false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 1));
    EXPECT_EQ(0, route_input_menu_repeat_tick_for_test(false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 1));
    EXPECT_EQ(1, route_input_menu_repeat_tick_for_test(false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 1));
    EXPECT_EQ(KEY_UP, keys[0]);

    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_RELEASE, { "cursor_up_down" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 2));
    EXPECT_EQ(0, route_input_menu_repeat_tick_for_test(false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 1));
}

TEST(RouteInputMenuStateTest, PreservesStateAcrossMenuPollCycles)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];

    route_input_reset_menu_keyboard_state_for_test();

    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "left_shift" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 2));
    EXPECT_EQ(1, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "a" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 2));
    EXPECT_EQ('A', keys[0]);

    EXPECT_EQ(0, route_input_menu_repeat_tick_for_test(false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 1));
    EXPECT_EQ(0, route_input_menu_repeat_tick_for_test(false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 1));
    EXPECT_EQ(1, route_input_menu_repeat_tick_for_test(false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 1));
    EXPECT_EQ('A', keys[0]);
}

TEST(RouteInputMenuStateTest, ClearsStateWhenMenuOwnershipEnds)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];
    uint8_t held_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    route_input_reset_menu_keyboard_state_for_test();

    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "left_shift" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    EXPECT_EQ(1, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "a" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    EXPECT_EQ('A', keys[0]);

    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "a" }), true, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    route_input_menu_snapshot_for_test(held_matrix);
    EXPECT_FALSE(key_active(held_matrix, "left_shift"));
    EXPECT_FALSE(key_active(held_matrix, "a"));

    EXPECT_EQ(1, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "a" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    EXPECT_EQ('a', keys[0]);
}

TEST(RouteInputMenuStateTest, ReleaseAllClearsHeldMenuSnapshot)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];
    uint8_t held_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    route_input_reset_menu_keyboard_state_for_test();
    EXPECT_EQ(0, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "left_shift" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));
    EXPECT_EQ(1, route_input_apply_menu_keyboard_event_for_test(
        keyboard_event(INPUT_PARSED_PRESS, { "a" }), false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 16));

    route_input_release_all_for_test();
    route_input_menu_snapshot_for_test(held_matrix);
    EXPECT_FALSE(key_active(held_matrix, "left_shift"));
    EXPECT_FALSE(key_active(held_matrix, "a"));
    EXPECT_EQ(0, route_input_menu_repeat_tick_for_test(false, keys, INPUT_API_MAX_KEYBOARD_INPUTS, 1));
}

TEST(RestJoystickStateTest, TapOverlayAutoReleasesWithoutClearingPersistentInput)
{
    reset_joystick_output();
    uint8_t hold[7] = { 0, 0, 0, 0, 1, 1, 1 };
    uint8_t port1 = 0;
    uint8_t port2 = 0;

    JoystickOutput::instance().setRestPort2Persistent(0x5E);
    JoystickOutput::instance().armRestPort2Overlay(0x0F, hold);
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x7F, port1);
    EXPECT_EQ(0x0E, port2);

    JoystickOutput::instance().tickOverlays();
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x5E, port2);
}

TEST(RestJoystickStateTest, ReleaseAllClearsBothPorts)
{
    reset_joystick_output();
    uint8_t hold[7] = { 1, 0, 0, 0, 0, 1, 1 };
    uint8_t port1 = 0;
    uint8_t port2 = 0;

    JoystickOutput::instance().setRestPort1Persistent(0x2F);
    JoystickOutput::instance().setRestPort2Persistent(0x5E);
    JoystickOutput::instance().armRestPort1Overlay(0x1E, hold);
    JoystickOutput::instance().releaseAllRest();
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x7F, port1);
    EXPECT_EQ(0x7F, port2);
}

TEST(RestJoystickStateTest, Fire2AndFire3PersistAndReleaseIndependently)
{
    reset_joystick_output();
    uint8_t port1 = 0;
    uint8_t port2 = 0;

    JoystickOutput::instance().setRestPort1Persistent(0x1F);
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x1F, port1);

    JoystickOutput::instance().setRestPort1Persistent(0x3F);
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x3F, port1);

    JoystickOutput::instance().setRestPort1Persistent(0x5F);
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x5F, port1);
}

TEST(RestJoystickStateTest, Fire2MapsToPotXAndFire3MapsToPotY)
{
    reset_joystick_output();
    uint8_t port1 = 0;
    uint8_t port2 = 0;
    uint8_t pot1x = 0;
    uint8_t pot1y = 0;
    uint8_t pot2x = 0;
    uint8_t pot2y = 0;

    JoystickOutput::instance().setRestPort2Persistent(0x5F);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x1F, port1);
    EXPECT_EQ(0x1F, port2);
    EXPECT_EQ(0x00, pot2x);
    EXPECT_EQ(0x80, pot2y);

    JoystickOutput::instance().setRestPort2Persistent(0x3F);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x80, pot2x);
    EXPECT_EQ(0x00, pot2y);
}
