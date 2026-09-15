#include "../../io/usb/tests/host_test/host_test.h"
#include "../../io/c64/keyboard_c64.h"
#include "../../io/usb/keyboard_usb.h"
#include "../../io/c64/joystick_output.h"
#include "../input_api.h"
#include "../route_input_menu.h"

#include <initializer_list>

// Drives Keyboard_C64::wait_free(), which busy-waits through wait_ms(). Letting
// the stub count calls and let go of the keys makes the wait deterministic.
struct WaitFreeProbe {
    int calls;
    int release_usb_at;          // 0 = never
    volatile uint8_t *row_register;
    int release_row_at;          // 0 = never
};

static WaitFreeProbe wait_free_probe = { 0, 0, 0, 0 };

extern "C" void wait_ms(int)
{
    wait_free_probe.calls++;
    if (wait_free_probe.release_usb_at && (wait_free_probe.calls >= wait_free_probe.release_usb_at)) {
        uint8_t release[USB_DATA_SIZE] = { 0 };
        wait_free_probe.release_usb_at = 0;
        system_usb_keyboard.process_data(release); // the user lets go of the USB key
    }
    if (wait_free_probe.release_row_at && (wait_free_probe.calls >= wait_free_probe.release_row_at)) {
        wait_free_probe.release_row_at = 0;
        *wait_free_probe.row_register = 0xFF;      // ... and of the C64 key
    }
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

// Two mice on port 1, told apart by address.
const int usb_mouse_a = 0;
const int usb_mouse_b = 0;

void reset_joystick_output(void)
{
    JoystickOutput::instance().clearUsbPort1Mouse();
    JoystickOutput::instance().setMouseFrameRate(50);
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

// A queued tap carrying both matrix keys and `restore` is what one
// `machine:input` request for C= plus RESTORE turns into. The C64 only reads
// the combination as C= plus RESTORE if the CBM column is already down in the
// matrix when the NMI edge arrives, so this asserts the hardware registers
// rather than the REST snapshot: applyMatrixState() writes matrix[0..7] before
// it writes the restore register at matrix[9], and the same tick sets both.
TEST(RestKeyboardStateTest, QueuedTapDrivesMatrixAndRestoreTogether)
{
    Keyboard_USB keyboard;
    volatile uint8_t hardware_matrix[11] = { 0 };
    uint8_t combo[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    const InputKeyboardMapEntry *cbm = find_keyboard_entry("commodore");
    ASSERT_TRUE(cbm != 0);
    add_key_to_matrix(combo, "commodore");

    keyboard.setMatrix(hardware_matrix);
    keyboard.enableMatrix(true);
    ASSERT_TRUE(keyboard.restQueueTap(combo, true, 2));

    EXPECT_TRUE((hardware_matrix[cbm->row] & (1 << cbm->col)) == 0);
    EXPECT_TRUE((hardware_matrix[9] & 1) == 0);

    keyboard.tickRestOverlays();
    EXPECT_TRUE((hardware_matrix[cbm->row] & (1 << cbm->col)) != 0);
    EXPECT_TRUE((hardware_matrix[9] & 1) != 0);

    keyboard.tickRestOverlays();
    EXPECT_TRUE((hardware_matrix[cbm->row] & (1 << cbm->col)) != 0);
    EXPECT_TRUE((hardware_matrix[9] & 1) != 0);

    keyboard.tickRestOverlays();
    EXPECT_TRUE((hardware_matrix[cbm->row] & (1 << cbm->col)) == 0);
    EXPECT_TRUE((hardware_matrix[9] & 1) == 0);
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

    // The tick that releases the first tap also starts the gap and spends its
    // first tick, so REST_TAP_GAP_TICKS - 1 further idle ticks follow before
    // the queued "b" may start. Ticked from the constant rather than from a
    // number written out again, so changing the rate cannot silently change
    // what this proves.
    keyboard.tickRestOverlays();
    keyboard.restSnapshot(matrix, restore);
    EXPECT_FALSE(key_active(matrix, "left_shift"));
    EXPECT_FALSE(key_active(matrix, "a"));
    EXPECT_FALSE(key_active(matrix, "b"));

    for (int gap = 1; gap < REST_TAP_GAP_TICKS; gap++) {
        keyboard.tickRestOverlays();
        keyboard.restSnapshot(matrix, restore);
        EXPECT_FALSE(key_active(matrix, "left_shift"));
        EXPECT_FALSE(key_active(matrix, "a"));
        EXPECT_FALSE(key_active(matrix, "b"));
    }

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
        // C=+R and Ctrl+R are the monitor's reset shortcut. They must not
        // resolve to KEY_DOWN, which is what the ASCII control code for R
        // (0x12) is: the monitor tests the reset shortcut before it reaches
        // its cursor-key handling, so a shared code would make cursor-down
        // reset the machine. 0x02 is the C= modifier flag and 0x04 is Ctrl.
        { "r", 0x00, 'r' },
        { "r", 0x02, KEY_CTRL_R },
        { "r", 0x04, KEY_CTRL_R },
    };
    EXPECT_TRUE(KEY_CTRL_R != KEY_DOWN);

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

// USB HID usage 0x29 (escape) maps to C64 matrix row 7, column 7: RUN/STOP.
static const uint8_t USB_KEY_ESCAPE = 0x29;
static const int MATRIX_RUNSTOP_ROW = 7;
static const uint8_t MATRIX_RUNSTOP_BIT = 0x80;

// Must outlive every test: the global keyboard keeps writing through this
// pointer once setMatrix() has been handed it.
static volatile uint8_t menu_exit_matrix[11] = { 0 };

namespace {

class AccessibleHost : public GenericHost
{
public:
    bool exists(void) { return true; }
    bool is_accessible(void) { return true; }
};

// system_usb_keyboard is global: never leave keys held for the next test.
void release_usb_keys(void)
{
    uint8_t release[USB_DATA_SIZE] = { 0x00 };
    system_usb_keyboard.process_data(release);
}

// The menu owns the keyboard and ESC is still held down: the state the firmware
// is in at the moment the user leaves the menu.
void arm_menu_with_escape_held(void)
{
    uint8_t escape[USB_DATA_SIZE] = { 0x00, 0x00, USB_KEY_ESCAPE, 0x00, 0x00, 0x00, 0x00, 0x00 };

    release_usb_keys();
    system_usb_keyboard.setMatrix(menu_exit_matrix);
    system_usb_keyboard.enableMatrix(false);
    system_usb_keyboard.process_data(escape);

    wait_free_probe.calls = 0;
    wait_free_probe.release_usb_at = 0;
    wait_free_probe.row_register = 0;
    wait_free_probe.release_row_at = 0;
}

} // namespace

// Why the wait exists: the matrix mirrors the live USB report, so a key that is
// still held when the menu gives the matrix back lands on the C64 right away.
// ESC is RUN/STOP, and the SID player returns to the menu on RUN/STOP.
TEST(KeyboardC64StateTest, HeldUsbEscapeIsRunStopOnTheC64Matrix)
{
    arm_menu_with_escape_held();
    EXPECT_EQ(0x00, menu_exit_matrix[MATRIX_RUNSTOP_ROW]);

    system_usb_keyboard.enableMatrix(true); // menu hands the matrix back
    EXPECT_EQ(MATRIX_RUNSTOP_BIT, menu_exit_matrix[MATRIX_RUNSTOP_ROW]);

    release_usb_keys();
}

TEST(KeyboardC64StateTest, MenuExitWaitsForHeldUsbKeyToBeReleased)
{
    Keyboard_C64 keyboard(NULL, NULL, NULL, NULL); // no host: only the USB wait runs

    arm_menu_with_escape_held();
    EXPECT_TRUE(system_usb_keyboard.anyKeyPressed());

    wait_free_probe.release_usb_at = 5;
    keyboard.wait_free();
    EXPECT_EQ(5, wait_free_probe.calls); // it really waited the exit key out
    EXPECT_FALSE(system_usb_keyboard.anyKeyPressed());

    system_usb_keyboard.enableMatrix(true);
    EXPECT_EQ(0x00, menu_exit_matrix[MATRIX_RUNSTOP_ROW]); // no phantom RUN/STOP

    release_usb_keys();
}

// Neither keyboard may cut the other's wait short: the added USB wait must not
// consume the budget the C64 matrix scan has always had.
TEST(KeyboardC64StateTest, MenuExitWaitsForBothKeyboards)
{
    AccessibleHost host;
    volatile uint8_t row_register = 0x7F; // a C64 key is down too
    volatile uint8_t col_register = 0xFF;
    Keyboard_C64 keyboard(&host, &row_register, &col_register, NULL);

    arm_menu_with_escape_held();
    wait_free_probe.row_register = &row_register;
    wait_free_probe.release_usb_at = 5;
    wait_free_probe.release_row_at = 9;

    keyboard.wait_free();
    EXPECT_EQ(9, wait_free_probe.calls); // waited past the USB release for the matrix
    EXPECT_EQ(0xFF, col_register);       // rows deselected again

    release_usb_keys();
}

TEST(KeyboardC64StateTest, MenuExitWaitIsBoundedWhenTheKeysAreNeverReleased)
{
    AccessibleHost host;
    volatile uint8_t row_register = 0x7F;
    volatile uint8_t col_register = 0xFF;
    Keyboard_C64 keyboard(&host, &row_register, &col_register, NULL);

    arm_menu_with_escape_held(); // both keys stay down forever

    keyboard.wait_free();
    // Worst case: each keyboard gets its own full budget, and no more.
    EXPECT_EQ(2 * KEYBOARD_WAIT_FREE_TIMEOUT_MS, wait_free_probe.calls);
    EXPECT_EQ(0xFF, col_register);

    release_usb_keys();
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

TEST(RestJoystickStateTest, ReleaseCancelsInFlightTapOverlayImmediately)
{
    // Tap then release of the same input, as route_input.cc applies them.
    reset_joystick_output();
    uint8_t hold[7] = { 0, 0, 0, 0, 0, 1, 0 };  // fire2 (bit 5) tapped
    uint8_t port1 = 0;
    uint8_t port2 = 0;

    // Tap shows fire2 pressed.
    JoystickOutput::instance().armRestPort1Overlay(0x7F & ~0x20, hold);
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x5F, port1);

    // Release must take effect immediately, not wait for the tap to expire.
    JoystickOutput::instance().setRestPort1Persistent(0x7F);
    JoystickOutput::instance().cancelRestPort1Overlay(0x20);
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x7F, port1);
}

TEST(RestJoystickStateTest, CancelOverlayLeavesOtherBitsAlone)
{
    reset_joystick_output();
    uint8_t hold[7] = { 0, 0, 0, 0, 0, 1, 1 };  // fire2 and fire3 both tapped
    uint8_t port1 = 0;
    uint8_t port2 = 0;

    JoystickOutput::instance().armRestPort1Overlay(0x7F & ~0x60, hold);
    JoystickOutput::instance().cancelRestPort1Overlay(0x20);  // cancel fire2 only
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x3F, port1);  // fire2 (bit 5) released, fire3 (bit 6) still mid-tap

    JoystickOutput::instance().tickOverlays();
    JoystickOutput::instance().snapshot(port1, port2);
    EXPECT_EQ(0x7F, port1);  // fire3's own countdown still auto-releases normally
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

// Issue #909: in Mouse + Wheel mode the wheel pulses port 1's left and right
// lines through setUsbPort1(). Each update rewrites the POT registers, which
// must keep the USB mouse position instead of the released joystick value.
TEST(UsbMouseJoystickOutputTest, MousePositionSurvivesPort1LineUpdates)
{
    reset_joystick_output();
    uint8_t port1 = 0;
    uint8_t port2 = 0;
    uint8_t pot1x = 0;
    uint8_t pot1y = 0;
    uint8_t pot2x = 0;
    uint8_t pot2y = 0;
    uint8_t hold[JOYSTICK_BUTTON_COUNT] = { 0 };

    JoystickOutput::instance().setUsbPort1Mouse(0x15, 0x2A, &usb_mouse_a);
    JoystickOutput::instance().setUsbPort1(0x1B);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x1B, port1);
    EXPECT_EQ(0x15, pot1x);
    EXPECT_EQ(0x2A, pot1y);
    EXPECT_EQ(0x80, pot2x);
    EXPECT_EQ(0x80, pot2y);

    JoystickOutput::instance().setUsbPort1(0x1F);
    JoystickOutput::instance().setRestPort2Persistent(0x1E);
    hold[0] = 1;
    JoystickOutput::instance().armRestPort1Overlay(0x1E, hold);
    JoystickOutput::instance().tickOverlays();
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x1F, port1);
    EXPECT_EQ(0x1E, port2);
    EXPECT_EQ(0x15, pot1x);
    EXPECT_EQ(0x2A, pot1y);
}

TEST(UsbMouseJoystickOutputTest, RestExtraButtonOwnsBothPort1PotLinesWhileHeld)
{
    reset_joystick_output();
    uint8_t port1 = 0;
    uint8_t port2 = 0;
    uint8_t pot1x = 0;
    uint8_t pot1y = 0;
    uint8_t pot2x = 0;
    uint8_t pot2y = 0;

    JoystickOutput::instance().setUsbPort1Mouse(0x15, 0x2A, &usb_mouse_a);
    JoystickOutput::instance().setRestPort1Persistent(0x5F);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x00, pot1x);
    EXPECT_EQ(0x80, pot1y);

    JoystickOutput::instance().setRestPort1Persistent(0x7F);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x15, pot1x);
    EXPECT_EQ(0x2A, pot1y);
}

TEST(UsbMouseJoystickOutputTest, ClearingTheMouseRestoresReleasedPotValues)
{
    reset_joystick_output();
    uint8_t port1 = 0;
    uint8_t port2 = 0;
    uint8_t pot1x = 0;
    uint8_t pot1y = 0;
    uint8_t pot2x = 0;
    uint8_t pot2y = 0;

    JoystickOutput::instance().setUsbPort1Mouse(0x15, 0x2A, &usb_mouse_a);
    JoystickOutput::instance().clearUsbPort1Mouse();
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x80, pot1x);
    EXPECT_EQ(0x80, pot1y);
}

// MousePotPacer: a 1351 driver samples the POT lines once per frame, so the
// changes shown within one budget window must add up to at most 63 counts.

// Drives a pacer on a millisecond clock, as the firmware does: reports when
// they are handled, and the pacing timer every 5ms. Every change is checked
// against the budget.
struct PacerRun {
    enum { HISTORY = 4096 };

    MousePotPacer pacer;
    int window;
    int now;
    int timer;
    int16_t x;
    int16_t y;
    int shown_x;
    int shown_y;
    int changes;
    int change_time[HISTORY];
    int change_x[HISTORY];
    int change_y[HISTORY];
    bool over_budget;

    explicit PacerRun(int window_ms) : window(window_ms), now(0), timer(0), x(0), y(0),
        shown_x(0), shown_y(0), changes(0), over_budget(false)
    {
        pacer.setWindow(window_ms);
        // Long enough ago that the jump reset() makes is out of the window.
        pacer.reset(0, 0, (uint16_t)-1000);
    }

    static int signed7(int difference)
    {
        difference &= 0x7F;
        return (difference >= 64) ? difference - 128 : difference;
    }

    void advance(void)
    {
        uint8_t before_x = pacer.potX();
        uint8_t before_y = pacer.potY();
        if (!pacer.advance((uint16_t)now)) {
            return;
        }
        int step_x = signed7(pacer.potX() - before_x);
        int step_y = signed7(pacer.potY() - before_y);
        shown_x += step_x;
        shown_y += step_y;
        if (changes >= HISTORY) {
            over_budget = true;
            return;
        }
        change_time[changes] = now;
        change_x[changes] = abs(step_x);
        change_y[changes] = abs(step_y);
        changes++;
        int sum_x = 0;
        int sum_y = 0;
        for (int i = changes - 1; (i >= 0) && (change_time[i] > now - window); i--) {
            sum_x += change_x[i];
            sum_y += change_y[i];
        }
        if ((sum_x > MousePotPacer::MAX_CHANGE) || (sum_y > MousePotPacer::MAX_CHANGE)) {
            over_budget = true;
        }
    }

    // Runs the pacing timer up to `until`.
    void runUntil(int until)
    {
        while (timer <= until) {
            now = timer;
            advance();
            timer += 5;
        }
        now = until;
    }

    // A report that moves by (dx, dy), handled at `at`.
    void report(int at, int dx, int dy)
    {
        runUntil(at);
        x += dx;
        y += dy;
        pacer.setTarget(x, y);
        advance();
    }

    void drain(void)
    {
        int stop = now + 1000;
        while (!pacer.settled() && (timer < stop)) {
            runUntil(timer);
        }
    }
};

TEST(MousePotPacerTest, FullReportsEvery20msGoOutAtOnceAt60Hz)
{
    PacerRun run(MousePotPacer::WINDOW_60HZ);
    for (int i = 0; i < 50; i++) {
        run.report(3 + (i * 20), 63, -63);
        EXPECT_TRUE(run.pacer.settled());
        EXPECT_EQ(run.x, run.shown_x);
        EXPECT_EQ(run.y, run.shown_y);
    }
    EXPECT_FALSE(run.over_budget);
}

TEST(MousePotPacerTest, HalfReportsEvery20msGoOutAtOnceAt50Hz)
{
    PacerRun run(MousePotPacer::WINDOW_50HZ);
    for (int i = 0; i < 50; i++) {
        run.report(3 + (i * 20), 31, 20);
        EXPECT_TRUE(run.pacer.settled());
        EXPECT_EQ(run.x, run.shown_x);
        EXPECT_EQ(run.y, run.shown_y);
    }
    EXPECT_FALSE(run.over_budget);
}

TEST(MousePotPacerTest, TwoFullReports20msApartAreSpreadAt50Hz)
{
    MousePotPacer pacer;
    pacer.setWindow(MousePotPacer::WINDOW_50HZ);
    pacer.reset(0, 0, 50);
    pacer.setTarget(63, 0);
    EXPECT_TRUE(pacer.advance(100));
    pacer.setTarget(126, 0);
    EXPECT_FALSE(pacer.advance(120));
    EXPECT_FALSE(pacer.advance(121));
    EXPECT_TRUE(pacer.advance(122));
    EXPECT_TRUE(pacer.settled());
    EXPECT_EQ(126 & 0x7F, pacer.potX());
}

TEST(MousePotPacerTest, BunchedReportsStayInsideTheBudgetAndLandExactly)
{
    for (int window = MousePotPacer::WINDOW_60HZ; window <= MousePotPacer::WINDOW_50HZ; window++) {
        PacerRun run(window);
        // Full reports every 20ms, handled up to 12ms late, with the wheel
        // adding 63 counts to every fifth. The late handling bunches reports
        // together; the average stays inside what the budget carries.
        static const int late[] = { 0, 12, 1, 11, 0, 6, 12, 0, 3, 9 };
        for (int i = 0; i < 40; i++) {
            run.report((i * 30) + late[i % 10], (i % 5 == 4) ? 126 : 63, -63);
        }
        run.drain();
        EXPECT_TRUE(run.pacer.settled());
        EXPECT_FALSE(run.over_budget);
        EXPECT_EQ(run.x, run.shown_x);
        EXPECT_EQ(run.y, run.shown_y);
    }
}

TEST(MousePotPacerTest, TheBacklogIsCappedSoThePointerStopsSoonAfterTheMouse)
{
    PacerRun run(MousePotPacer::WINDOW_50HZ);
    // Two seconds of 63 and 126 counts every 20ms, more than the 22ms window,
    // with its releases on 5ms timer runs, carries.
    for (int i = 0; i < 100; i++) {
        run.report(i * 20, 63, (i % 2) ? 126 : 63);
    }
    int stopped_at = run.now;
    run.drain();
    EXPECT_TRUE(run.pacer.settled());
    EXPECT_FALSE(run.over_budget);
    EXPECT_TRUE(run.shown_y < run.y);
    // 252 counts are four windows, each ending on a 5ms timer run.
    EXPECT_TRUE((run.timer - stopped_at) <= 4 * (MousePotPacer::WINDOW_50HZ + 5) + 5);
}

TEST(MousePotPacerTest, AReversalTurnsBackWithoutOverrunningTheBudget)
{
    PacerRun run(MousePotPacer::WINDOW_50HZ);
    run.report(0, 100, 0);
    EXPECT_EQ(63, run.shown_x);
    run.report(10, -110, 0);
    EXPECT_EQ(63, run.shown_x);
    run.drain();
    EXPECT_FALSE(run.over_budget);
    EXPECT_EQ(-10, run.shown_x);
}

TEST(MousePotPacerTest, ThePositionMayWrapPastInt16)
{
    MousePotPacer pacer;
    pacer.reset(32760, 0, 0);
    pacer.setTarget((int16_t)-32766, 0);
    EXPECT_TRUE(pacer.advance(100));
    EXPECT_TRUE(pacer.settled());
    EXPECT_EQ((uint8_t)((-32766) & 0x7F), pacer.potX());
}

TEST(MousePotPacerTest, TheBudgetHoldsAcrossTheMillisecondClockWrap)
{
    MousePotPacer pacer;
    pacer.setWindow(MousePotPacer::WINDOW_50HZ);
    pacer.reset(0, 0, 65000);
    pacer.setTarget(63, 0);
    EXPECT_TRUE(pacer.advance(65530));
    pacer.setTarget(126, 0);
    EXPECT_FALSE(pacer.advance(10));
    EXPECT_FALSE(pacer.advance(15));
    EXPECT_TRUE(pacer.advance(16));
    EXPECT_EQ(126 & 0x7F, pacer.potX());
}

TEST(MousePotPacerTest, TheJumpAResetMakesSpendsItsWindow)
{
    MousePotPacer pacer;
    pacer.setWindow(MousePotPacer::WINDOW_50HZ);
    pacer.reset(63, 0, 1000);
    pacer.setTarget(126, 0);
    EXPECT_FALSE(pacer.advance(1015));
    EXPECT_FALSE(pacer.advance(1021));
    EXPECT_TRUE(pacer.advance(1022));
    EXPECT_EQ(126 & 0x7F, pacer.potX());
}

// An oracle of its own: reports are handled up to 15ms late, the pacing timer
// runs every 5ms, and a driver reads the lines once per frame from a random
// phase, seeing them as they were up to 0.52ms earlier (the SID's measuring
// cycle). No two reads may differ by more than 63 counts.
static int worst_frame_step(int window, double frame_ms, int counts_per_report, unsigned seed)
{
    MousePotPacer pacer;
    pacer.setWindow(window);
    pacer.reset(0, 0, 0);
    unsigned lcg = seed;
    struct Change { double at; int shown; };
    static Change changes[4096];
    int change_count = 0;
    int16_t position = 0;
    int shown = 0;
    double report_at = 100.0;
    double timer_at = 0.0;
    changes[change_count++] = (Change){ 0.0, 0 };
    for (int report = 0; report < 400; report++) {
        lcg = lcg * 1103515245u + 12345u;
        double handled = report_at + ((lcg >> 16) % 150) / 10.0;
        while (timer_at < handled) {
            if (pacer.advance((uint16_t)timer_at)) {
                shown += PacerRun::signed7(pacer.potX() - (shown & 0x7F));
                changes[change_count++] = (Change){ timer_at, shown };
            }
            timer_at += 5.0;
        }
        position = (int16_t)(position + counts_per_report);
        pacer.setTarget(position, 0);
        if (pacer.advance((uint16_t)handled)) {
            shown += PacerRun::signed7(pacer.potX() - (shown & 0x7F));
            changes[change_count++] = (Change){ handled, shown };
        }
        report_at += 20.0;
    }
    for (int i = 0; i < 400; i++, timer_at += 5.0) {
        if (pacer.advance((uint16_t)timer_at)) {
            shown += PacerRun::signed7(pacer.potX() - (shown & 0x7F));
            changes[change_count++] = (Change){ timer_at, shown };
        }
    }
    int worst = 0;
    for (int phase = 0; phase < 20; phase++) {
        double read_at = 100.0 + phase * frame_ms / 20.0;
        int previous = 0;
        bool first = true;
        for (; read_at < timer_at; read_at += frame_ms) {
            lcg = lcg * 1103515245u + 12345u;
            double seen_at = read_at - ((lcg >> 16) % 53) / 100.0;
            int value = 0;
            for (int i = 0; (i < change_count) && (changes[i].at <= seen_at); i++) {
                value = changes[i].shown;
            }
            if (!first && (abs(value - previous) > worst)) {
                worst = abs(value - previous);
            }
            previous = value;
            first = false;
        }
    }
    return worst;
}

TEST(MousePotPacerTest, NoFrameReadsMoreThan63Counts)
{
    for (unsigned seed = 1; seed <= 4; seed++) {
        EXPECT_TRUE(worst_frame_step(MousePotPacer::WINDOW_50HZ, 1000.0 / 50.124, 126, seed) <= 63);
        EXPECT_TRUE(worst_frame_step(MousePotPacer::WINDOW_50HZ, 1000.0 / 50.124, 63, seed) <= 63);
        EXPECT_TRUE(worst_frame_step(MousePotPacer::WINDOW_60HZ, 1000.0 / 59.826, 126, seed) <= 63);
        EXPECT_TRUE(worst_frame_step(MousePotPacer::WINDOW_60HZ, 1000.0 / 59.826, 63, seed) <= 63);
    }
}

TEST(MousePotPacerTest, AQuietSpellFreesTheWholeBudget)
{
    MousePotPacer pacer;
    pacer.reset(0, 0, 0);
    pacer.setTarget(63, 0);
    EXPECT_TRUE(pacer.advance(50));
    pacer.setTarget(126, 0);
    EXPECT_TRUE(pacer.advance(5000));
    EXPECT_EQ(126 & 0x7F, pacer.potX());
}

TEST(UsbMouseJoystickOutputTest, PotOutputFollowsThePacer)
{
    reset_joystick_output();
    uint8_t port1 = 0;
    uint8_t port2 = 0;
    uint8_t pot1x = 0;
    uint8_t pot1y = 0;
    uint8_t pot2x = 0;
    uint8_t pot2y = 0;

    // Off the device the clock setUsbPort1Mouse() reads stands at 0ms. The first
    // report spends the budget of its window, so the next waits a window.
    JoystickOutput::instance().setUsbPort1Mouse(0, 0, &usb_mouse_a);
    JoystickOutput::instance().setUsbPort1Mouse(100, 0, &usb_mouse_a);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0, pot1x);
    JoystickOutput::instance().tickMouse(MousePotPacer::WINDOW_50HZ - 1);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0, pot1x);
    JoystickOutput::instance().tickMouse(MousePotPacer::WINDOW_50HZ);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(63, pot1x);
    JoystickOutput::instance().tickMouse(2 * MousePotPacer::WINDOW_50HZ);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(100, pot1x);
}

// A second mouse has its own running position; taking over the POT lines must
// not turn the distance between the two positions into movement.
TEST(UsbMouseJoystickOutputTest, AnotherMouseTakesOverWithoutMovingThePointer)
{
    reset_joystick_output();
    uint8_t port1 = 0;
    uint8_t port2 = 0;
    uint8_t pot1x = 0;
    uint8_t pot1y = 0;
    uint8_t pot2x = 0;
    uint8_t pot2y = 0;

    JoystickOutput::instance().setUsbPort1Mouse(0x15, 0x2A, &usb_mouse_a);
    JoystickOutput::instance().setUsbPort1Mouse(0x55, 0x2A, &usb_mouse_b);
    JoystickOutput::instance().tickMouse(100);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x15, pot1x);
    JoystickOutput::instance().setUsbPort1Mouse(0x5A, 0x2A, &usb_mouse_b);
    JoystickOutput::instance().tickMouse(200);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(0x1A, pot1x);
}

TEST(UsbMouseJoystickOutputTest, TheFrameRateSetsThePacingWindow)
{
    reset_joystick_output();
    uint8_t port1 = 0;
    uint8_t port2 = 0;
    uint8_t pot1x = 0;
    uint8_t pot1y = 0;
    uint8_t pot2x = 0;
    uint8_t pot2y = 0;

    JoystickOutput::instance().setMouseFrameRate(60);
    JoystickOutput::instance().setUsbPort1Mouse(0, 0, &usb_mouse_a);
    JoystickOutput::instance().setUsbPort1Mouse(0, 100, &usb_mouse_a);
    JoystickOutput::instance().tickMouse(MousePotPacer::WINDOW_60HZ);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(63, pot1y);
    JoystickOutput::instance().tickMouse(2 * MousePotPacer::WINDOW_60HZ);
    JoystickOutput::instance().outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    EXPECT_EQ(100, pot1y);
}
