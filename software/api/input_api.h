#ifndef INPUT_API_H
#define INPUT_API_H

#include "json.h"
#include "integer.h"

#include <stdio.h>
#include <string.h>

static const int INPUT_API_MAX_EVENTS = 64;
static const int INPUT_API_MAX_KEYBOARD_INPUTS = 8;
static const int INPUT_API_MAX_JOYSTICK_INPUTS = 7;
static const int INPUT_API_ERROR_SIZE = 160;
// JSON values a request may hold: the tokenizer's limit.
static const int INPUT_API_MAX_JSON_TOKENS = 1024;
// A mouse report moves by at most this many HID counts on each axis, as the
// eight-bit relative fields of a USB mouse report do.
static const int INPUT_API_MAX_MOUSE_MOTION = 127;
// Wheel detents in one event, sent as one report each.
static const int INPUT_API_MAX_MOUSE_DETENTS = 64;
// Steps in one `path`. Each step costs three JSON tokens of the 1024 a request
// may use.
static const int INPUT_API_MAX_MOUSE_PATH_STEPS = 256;
// A path step every 20ms at most, as often as the firmware polls a USB mouse.
static const int INPUT_API_MIN_MOUSE_PATH_INTERVAL_MS = 20;
static const int INPUT_API_MAX_MOUSE_PATH_INTERVAL_MS = 1000;
static const int INPUT_API_DEFAULT_MOUSE_PATH_INTERVAL_MS = 20;

enum InputParsedKind {
    INPUT_PARSED_KEYBOARD,
    INPUT_PARSED_JOYSTICK,
    INPUT_PARSED_RELEASE_ALL,
    INPUT_PARSED_MOUSE_BUTTONS,
    INPUT_PARSED_MOUSE_MOVE,
    INPUT_PARSED_MOUSE_WHEEL,
    INPUT_PARSED_MOUSE_PATH
};

enum InputParsedTransition {
    INPUT_PARSED_PRESS,
    INPUT_PARSED_RELEASE,
    INPUT_PARSED_TAP
};

struct InputKeyboardMapEntry {
    const char *name;
    uint8_t row;
    uint8_t col;
    bool restore;
};

struct InputJoystickMapEntry {
    const char *name;
    uint8_t bit;
};

struct InputMouseButtonMapEntry {
    const char *name;
    uint8_t mask;               // bit in a HID mouse report's button byte
};

struct InputParsedEvent {
    InputParsedKind kind;
    InputParsedTransition transition;
    uint8_t port;
    uint8_t keyboard_count;
    uint8_t keyboard_index[INPUT_API_MAX_KEYBOARD_INPUTS];
    uint8_t joystick_mask;
    uint8_t mouse_buttons;
    int16_t mouse_x;            // move
    int16_t mouse_y;
    int16_t wheel_vertical;     // wheel, in detents; positive is away from the user
    int16_t wheel_horizontal;   // positive is to the right
    JSON_List *path;            // path: validated [x, y] steps, owned by the request
    uint16_t path_steps;
    uint16_t path_interval_ms;
};

static const InputKeyboardMapEntry INPUT_API_KEYBOARD_MAP[] = {
    { "inst_del", 0, 0, false },
    { "return", 0, 1, false },
    { "cursor_left_right", 0, 2, false },
    { "f7", 0, 3, false },
    { "f1", 0, 4, false },
    { "f3", 0, 5, false },
    { "f5", 0, 6, false },
    { "cursor_up_down", 0, 7, false },
    { "3", 1, 0, false },
    { "w", 1, 1, false },
    { "a", 1, 2, false },
    { "4", 1, 3, false },
    { "z", 1, 4, false },
    { "s", 1, 5, false },
    { "e", 1, 6, false },
    { "left_shift", 1, 7, false },
    { "5", 2, 0, false },
    { "r", 2, 1, false },
    { "d", 2, 2, false },
    { "6", 2, 3, false },
    { "c", 2, 4, false },
    { "f", 2, 5, false },
    { "t", 2, 6, false },
    { "x", 2, 7, false },
    { "7", 3, 0, false },
    { "y", 3, 1, false },
    { "g", 3, 2, false },
    { "8", 3, 3, false },
    { "b", 3, 4, false },
    { "h", 3, 5, false },
    { "u", 3, 6, false },
    { "v", 3, 7, false },
    { "9", 4, 0, false },
    { "i", 4, 1, false },
    { "j", 4, 2, false },
    { "0", 4, 3, false },
    { "m", 4, 4, false },
    { "k", 4, 5, false },
    { "o", 4, 6, false },
    { "n", 4, 7, false },
    { "plus", 5, 0, false },
    { "p", 5, 1, false },
    { "l", 5, 2, false },
    { "minus", 5, 3, false },
    { "period", 5, 4, false },
    { "colon", 5, 5, false },
    { "at", 5, 6, false },
    { "comma", 5, 7, false },
    { "pound", 6, 0, false },
    { "star", 6, 1, false },
    { "semicolon", 6, 2, false },
    { "clr_home", 6, 3, false },
    { "right_shift", 6, 4, false },
    { "equals", 6, 5, false },
    { "arrow_up", 6, 6, false },
    { "slash", 6, 7, false },
    { "1", 7, 0, false },
    { "arrow_left", 7, 1, false },
    { "ctrl", 7, 2, false },
    { "2", 7, 3, false },
    { "space", 7, 4, false },
    { "commodore", 7, 5, false },
    { "q", 7, 6, false },
    { "run_stop", 7, 7, false },
    { "restore", 0, 0, true },
};

static const InputJoystickMapEntry INPUT_API_JOYSTICK_MAP[] = {
    { "up", 0 },
    { "down", 1 },
    { "left", 2 },
    { "right", 3 },
    { "fire", 4 },
    { "fire2", 5 },
    { "fire3", 6 },
};

static const InputMouseButtonMapEntry INPUT_API_MOUSE_BUTTON_MAP[] = {
    { "left", 0x01 },
    { "right", 0x02 },
    { "middle", 0x04 },
};

static const int INPUT_API_KEYBOARD_MAP_COUNT = sizeof(INPUT_API_KEYBOARD_MAP) / sizeof(INPUT_API_KEYBOARD_MAP[0]);
static const int INPUT_API_MOUSE_BUTTON_MAP_COUNT = sizeof(INPUT_API_MOUSE_BUTTON_MAP) / sizeof(INPUT_API_MOUSE_BUTTON_MAP[0]);
static const int INPUT_API_JOYSTICK_MAP_COUNT = sizeof(INPUT_API_JOYSTICK_MAP) / sizeof(INPUT_API_JOYSTICK_MAP[0]);

static inline const InputKeyboardMapEntry *input_api_keyboard_map(void)
{
    return INPUT_API_KEYBOARD_MAP;
}

static inline int input_api_keyboard_map_count(void)
{
    return INPUT_API_KEYBOARD_MAP_COUNT;
}

static inline const InputJoystickMapEntry *input_api_joystick_map(void)
{
    return INPUT_API_JOYSTICK_MAP;
}

static inline int input_api_joystick_map_count(void)
{
    return INPUT_API_JOYSTICK_MAP_COUNT;
}

static inline void input_api_copy_preview(char *dst, size_t dst_size, const char *src)
{
    if (!dst || (dst_size == 0)) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    size_t i = 0;
    while (src[i] && ((i + 1) < dst_size)) {
        dst[i] = src[i];
        i++;
    }
    if (src[i] && (dst_size >= 4)) {
        dst[dst_size - 4] = '.';
        dst[dst_size - 3] = '.';
        dst[dst_size - 2] = '.';
        dst[dst_size - 1] = 0;
        return;
    }
    dst[i] = 0;
}

static inline void input_api_set_error(char *err, size_t err_size, const char *msg)
{
    input_api_copy_preview(err, err_size, msg);
}

static inline bool input_api_has_key(JSON_Object *obj, const char *name)
{
    return obj->get(name) != NULL;
}

static inline bool input_api_key_allowed(const char *key, const char *const *allowed, int allowed_count)
{
    for (int i = 0; i < allowed_count; i++) {
        if (strcmp(key, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static inline bool input_api_reject_unknown_keys(JSON_Object *obj, const char *const *allowed, int allowed_count,
    char *err, size_t err_size)
{
    IndexedList<const char *> *keys = obj->get_keys();
    for (int i = 0; i < keys->get_elements(); i++) {
        const char *key = (*keys)[i];
        if (!input_api_key_allowed(key, allowed, allowed_count)) {
            char preview[32];
            input_api_copy_preview(preview, sizeof(preview), key);
            sprintf(err, "Unknown field `%s`.", preview);
            return false;
        }
    }
    return true;
}

static inline bool input_api_get_string_field(JSON_Object *obj, const char *name, const char **out,
    char *err, size_t err_size)
{
    JSON *value = obj->get(name);
    if (!value) {
        sprintf(err, "`%s` is required.", name);
        return false;
    }
    if (value->type() != eString) {
        sprintf(err, "`%s` must be a string.", name);
        return false;
    }
    *out = ((JSON_String *)value)->get_string();
    return true;
}

static inline bool input_api_parse_transition(JSON_Object *obj, InputParsedTransition &transition,
    char *err, size_t err_size)
{
    const char *name;
    if (!input_api_get_string_field(obj, "transition", &name, err, err_size)) {
        return false;
    }
    if (strcmp(name, "press") == 0) {
        transition = INPUT_PARSED_PRESS;
        return true;
    }
    if (strcmp(name, "release") == 0) {
        transition = INPUT_PARSED_RELEASE;
        return true;
    }
    if (strcmp(name, "tap") == 0) {
        transition = INPUT_PARSED_TAP;
        return true;
    }
    input_api_set_error(err, err_size, "`transition` must be one of `press`, `release`, or `tap`.");
    return false;
}

static inline int input_api_find_keyboard_input(const char *name)
{
    for (int i = 0; i < INPUT_API_KEYBOARD_MAP_COUNT; i++) {
        if (strcmp(name, INPUT_API_KEYBOARD_MAP[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static inline int input_api_find_joystick_input(const char *name)
{
    for (int i = 0; i < INPUT_API_JOYSTICK_MAP_COUNT; i++) {
        if (strcmp(name, INPUT_API_JOYSTICK_MAP[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static inline bool input_api_parse_keyboard_event(JSON_Object *obj, InputParsedEvent &out, char *err, size_t err_size)
{
    static const char *const allowed[] = { "kind", "inputs", "transition" };
    if (!input_api_reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err, err_size)) {
        return false;
    }
    if (input_api_has_key(obj, "port")) {
        input_api_set_error(err, err_size, "`port` is not allowed for keyboard events.");
        return false;
    }
    if (!input_api_parse_transition(obj, out.transition, err, err_size)) {
        return false;
    }
    JSON *inputs = obj->get("inputs");
    if (!inputs) {
        input_api_set_error(err, err_size, "`inputs` is required.");
        return false;
    }
    if (inputs->type() != eList) {
        input_api_set_error(err, err_size, "`inputs` must be an array.");
        return false;
    }
    JSON_List *list = (JSON_List *)inputs;
    int count = list->get_num_elements();
    if ((count < 1) || (count > INPUT_API_MAX_KEYBOARD_INPUTS)) {
        input_api_set_error(err, err_size, "`inputs` must contain 1..8 entries.");
        return false;
    }

    bool seen[INPUT_API_KEYBOARD_MAP_COUNT] = { false };
    bool has_restore = false;
    out.kind = INPUT_PARSED_KEYBOARD;
    out.keyboard_count = count;
    for (int i = 0; i < count; i++) {
        JSON *entry = (*list)[i];
        if (entry->type() != eString) {
            input_api_set_error(err, err_size, "Keyboard inputs must be strings.");
            return false;
        }
        const char *name = ((JSON_String *)entry)->get_string();
        int index = input_api_find_keyboard_input(name);
        if (index < 0) {
            char preview[32];
            input_api_copy_preview(preview, sizeof(preview), name);
            sprintf(err, "`%s` is not a valid keyboard input.", preview);
            return false;
        }
        if (seen[index]) {
            char preview[32];
            input_api_copy_preview(preview, sizeof(preview), name);
            sprintf(err, "`%s` appears more than once in `inputs`.", preview);
            return false;
        }
        seen[index] = true;
        out.keyboard_index[i] = index;
        if (INPUT_API_KEYBOARD_MAP[index].restore) {
            has_restore = true;
        }
    }

    // `restore` drives the NMI line rather than a keyboard matrix column, so it
    // is an edge and not a level. `press` and `release` have nothing to act on:
    // route_input.cc skips the restore entry when it builds the live matrix for
    // both transitions, so accepting them here would return 200 and do nothing.
    // Matrix keys alongside it are a different case and are allowed: a queued
    // tap asserts the matrix columns and the restore line from one call, and
    // Keyboard_USB::applyMatrixState() writes the matrix rows before the
    // restore register, which is the order C= plus RESTORE and RUN/STOP plus
    // RESTORE need.
    if (has_restore && (out.transition != INPUT_PARSED_TAP)) {
        input_api_set_error(err, err_size, "`restore` is only valid with transition `tap`.");
        return false;
    }
    return true;
}

static inline bool input_api_parse_joystick_event(JSON_Object *obj, InputParsedEvent &out, char *err, size_t err_size)
{
    static const char *const allowed[] = { "kind", "port", "inputs", "transition" };
    if (!input_api_reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err, err_size)) {
        return false;
    }
    if (!input_api_parse_transition(obj, out.transition, err, err_size)) {
        return false;
    }
    JSON *port = obj->get("port");
    if (!port) {
        input_api_set_error(err, err_size, "`port` is required.");
        return false;
    }
    if (port->type() != eInteger) {
        input_api_set_error(err, err_size, "`port` must be an integer.");
        return false;
    }
    int port_value = ((JSON_Integer *)port)->get_value();
    if ((port_value != 1) && (port_value != 2)) {
        input_api_set_error(err, err_size, "`port` must be 1 or 2.");
        return false;
    }

    JSON *inputs = obj->get("inputs");
    if (!inputs) {
        input_api_set_error(err, err_size, "`inputs` is required.");
        return false;
    }
    if (inputs->type() != eList) {
        input_api_set_error(err, err_size, "`inputs` must be an array.");
        return false;
    }
    JSON_List *list = (JSON_List *)inputs;
    int count = list->get_num_elements();
    if ((count < 1) || (count > INPUT_API_MAX_JOYSTICK_INPUTS)) {
        input_api_set_error(err, err_size, "`inputs` must contain 1..7 entries.");
        return false;
    }

    bool seen[INPUT_API_JOYSTICK_MAP_COUNT] = { false };
    out.kind = INPUT_PARSED_JOYSTICK;
    out.port = port_value;
    out.joystick_mask = 0;
    for (int i = 0; i < count; i++) {
        JSON *entry = (*list)[i];
        if (entry->type() != eString) {
            input_api_set_error(err, err_size, "Joystick inputs must be strings.");
            return false;
        }
        const char *name = ((JSON_String *)entry)->get_string();
        int index = input_api_find_joystick_input(name);
        if (index < 0) {
            char preview[32];
            input_api_copy_preview(preview, sizeof(preview), name);
            sprintf(err, "`%s` is not a valid joystick input.", preview);
            return false;
        }
        if (seen[index]) {
            char preview[32];
            input_api_copy_preview(preview, sizeof(preview), name);
            sprintf(err, "`%s` appears more than once in `inputs`.", preview);
            return false;
        }
        seen[index] = true;
        out.joystick_mask |= (1 << INPUT_API_JOYSTICK_MAP[index].bit);
    }
    return true;
}

static inline bool input_api_get_int_member(JSON_Object *obj, const char *object_name, const char *name,
    int low, int high, bool required, int &out, char *err, size_t err_size)
{
    JSON *value = obj->get(name);
    if (!value) {
        if (required) {
            sprintf(err, "`%s.%s` is required.", object_name, name);
            return false;
        }
        out = 0;
        return true;
    }
    if (value->type() != eInteger) {
        sprintf(err, "`%s.%s` must be an integer.", object_name, name);
        return false;
    }
    out = ((JSON_Integer *)value)->get_value();
    if ((out < low) || (out > high)) {
        sprintf(err, "`%s.%s` must be %d..%d.", object_name, name, low, high);
        return false;
    }
    return true;
}

static inline JSON_Object *input_api_get_object_field(JSON_Object *obj, const char *name,
    const char *const *allowed, int allowed_count, char *err, size_t err_size)
{
    JSON *value = obj->get(name);
    if (value->type() != eObject) {
        sprintf(err, "`%s` must be an object.", name);
        return NULL;
    }
    JSON_Object *member = (JSON_Object *)value;
    IndexedList<const char *> *keys = member->get_keys();
    for (int i = 0; i < keys->get_elements(); i++) {
        if (!input_api_key_allowed((*keys)[i], allowed, allowed_count)) {
            char preview[32];
            input_api_copy_preview(preview, sizeof(preview), (*keys)[i]);
            sprintf(err, "Unknown field `%s.%s`.", name, preview);
            return NULL;
        }
    }
    return member;
}

static inline bool input_api_parse_mouse_buttons(JSON_Object *obj, InputParsedEvent &out, char *err, size_t err_size)
{
    if (!input_api_parse_transition(obj, out.transition, err, err_size)) {
        return false;
    }
    JSON *inputs = obj->get("inputs");
    if (inputs->type() != eList) {
        input_api_set_error(err, err_size, "`inputs` must be an array.");
        return false;
    }
    JSON_List *list = (JSON_List *)inputs;
    int count = list->get_num_elements();
    if ((count < 1) || (count > INPUT_API_MOUSE_BUTTON_MAP_COUNT)) {
        input_api_set_error(err, err_size, "`inputs` must contain 1..3 entries.");
        return false;
    }
    out.kind = INPUT_PARSED_MOUSE_BUTTONS;
    out.mouse_buttons = 0;
    for (int i = 0; i < count; i++) {
        JSON *entry = (*list)[i];
        if (entry->type() != eString) {
            input_api_set_error(err, err_size, "Mouse inputs must be strings.");
            return false;
        }
        const char *name = ((JSON_String *)entry)->get_string();
        uint8_t mask = 0;
        for (int b = 0; b < INPUT_API_MOUSE_BUTTON_MAP_COUNT; b++) {
            if (strcmp(name, INPUT_API_MOUSE_BUTTON_MAP[b].name) == 0) {
                mask = INPUT_API_MOUSE_BUTTON_MAP[b].mask;
            }
        }
        char preview[32];
        input_api_copy_preview(preview, sizeof(preview), name);
        if (!mask) {
            sprintf(err, "`%s` is not a valid mouse input.", preview);
            return false;
        }
        if (out.mouse_buttons & mask) {
            sprintf(err, "`%s` appears more than once in `inputs`.", preview);
            return false;
        }
        out.mouse_buttons |= mask;
    }
    return true;
}

static inline bool input_api_parse_mouse_move(JSON_Object *obj, InputParsedEvent &out, char *err, size_t err_size)
{
    static const char *const allowed[] = { "x", "y" };
    JSON_Object *move = input_api_get_object_field(obj, "move", allowed, 2, err, err_size);
    int x, y;
    if (!move ||
        !input_api_get_int_member(move, "move", "x", -INPUT_API_MAX_MOUSE_MOTION, INPUT_API_MAX_MOUSE_MOTION,
            false, x, err, err_size) ||
        !input_api_get_int_member(move, "move", "y", -INPUT_API_MAX_MOUSE_MOTION, INPUT_API_MAX_MOUSE_MOTION,
            false, y, err, err_size)) {
        return false;
    }
    if (!input_api_has_key(move, "x") && !input_api_has_key(move, "y")) {
        input_api_set_error(err, err_size, "`move` must have `x` or `y`.");
        return false;
    }
    out.kind = INPUT_PARSED_MOUSE_MOVE;
    out.mouse_x = x;
    out.mouse_y = y;
    return true;
}

static inline bool input_api_parse_mouse_wheel(JSON_Object *obj, InputParsedEvent &out, char *err, size_t err_size)
{
    static const char *const allowed[] = { "vertical", "horizontal" };
    JSON_Object *wheel = input_api_get_object_field(obj, "wheel", allowed, 2, err, err_size);
    int vertical, horizontal;
    if (!wheel ||
        !input_api_get_int_member(wheel, "wheel", "vertical", -INPUT_API_MAX_MOUSE_DETENTS,
            INPUT_API_MAX_MOUSE_DETENTS, false, vertical, err, err_size) ||
        !input_api_get_int_member(wheel, "wheel", "horizontal", -INPUT_API_MAX_MOUSE_DETENTS,
            INPUT_API_MAX_MOUSE_DETENTS, false, horizontal, err, err_size)) {
        return false;
    }
    if (!vertical && !horizontal) {
        input_api_set_error(err, err_size, "`wheel` must turn at least one wheel.");
        return false;
    }
    out.kind = INPUT_PARSED_MOUSE_WHEEL;
    out.wheel_vertical = vertical;
    out.wheel_horizontal = horizontal;
    return true;
}

static inline bool input_api_parse_mouse_path(JSON_Object *obj, InputParsedEvent &out, char *err, size_t err_size)
{
    JSON *path = obj->get("path");
    if (path->type() != eList) {
        input_api_set_error(err, err_size, "`path` must be an array of [x, y] steps.");
        return false;
    }
    JSON_List *steps = (JSON_List *)path;
    int count = steps->get_num_elements();
    if ((count < 1) || (count > INPUT_API_MAX_MOUSE_PATH_STEPS)) {
        sprintf(err, "`path` must contain 1..%d steps.", INPUT_API_MAX_MOUSE_PATH_STEPS);
        return false;
    }
    for (int i = 0; i < count; i++) {
        JSON *step = (*steps)[i];
        if ((step->type() != eList) || (((JSON_List *)step)->get_num_elements() != 2)) {
            sprintf(err, "`path[%d]` must be an [x, y] pair.", i);
            return false;
        }
        for (int axis = 0; axis < 2; axis++) {
            JSON *value = (*(JSON_List *)step)[axis];
            if (value->type() != eInteger) {
                sprintf(err, "`path[%d]` must hold integers.", i);
                return false;
            }
            int v = ((JSON_Integer *)value)->get_value();
            if ((v < -INPUT_API_MAX_MOUSE_MOTION) || (v > INPUT_API_MAX_MOUSE_MOTION)) {
                sprintf(err, "`path[%d]` values must be -%d..%d.", i, INPUT_API_MAX_MOUSE_MOTION,
                    INPUT_API_MAX_MOUSE_MOTION);
                return false;
            }
        }
    }
    int interval = INPUT_API_DEFAULT_MOUSE_PATH_INTERVAL_MS;
    if (input_api_has_key(obj, "interval_ms")) {
        JSON *value = obj->get("interval_ms");
        if (value->type() != eInteger) {
            input_api_set_error(err, err_size, "`interval_ms` must be an integer.");
            return false;
        }
        interval = ((JSON_Integer *)value)->get_value();
        if ((interval < INPUT_API_MIN_MOUSE_PATH_INTERVAL_MS) || (interval > INPUT_API_MAX_MOUSE_PATH_INTERVAL_MS)) {
            sprintf(err, "`interval_ms` must be %d..%d.", INPUT_API_MIN_MOUSE_PATH_INTERVAL_MS,
                INPUT_API_MAX_MOUSE_PATH_INTERVAL_MS);
            return false;
        }
    }
    out.kind = INPUT_PARSED_MOUSE_PATH;
    out.path = steps;
    out.path_steps = count;
    out.path_interval_ms = interval;
    return true;
}

// A mouse event does exactly one thing: buttons (`inputs` with a
// `transition`), one `move`, a `wheel` turn, or a `path` of moves.
static inline bool input_api_parse_mouse_event(JSON_Object *obj, InputParsedEvent &out, char *err, size_t err_size)
{
    static const char *const allowed[] = { "kind", "inputs", "transition", "move", "wheel", "path", "interval_ms" };
    if (!input_api_reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err, err_size)) {
        return false;
    }
    bool inputs = input_api_has_key(obj, "inputs");
    bool move = input_api_has_key(obj, "move");
    bool wheel = input_api_has_key(obj, "wheel");
    bool path = input_api_has_key(obj, "path");
    if ((int)inputs + (int)move + (int)wheel + (int)path != 1) {
        input_api_set_error(err, err_size, "A mouse event needs exactly one of `inputs`, `move`, `wheel`, or `path`.");
        return false;
    }
    if (!inputs && input_api_has_key(obj, "transition")) {
        input_api_set_error(err, err_size, "`transition` is only valid with `inputs`.");
        return false;
    }
    if (!path && input_api_has_key(obj, "interval_ms")) {
        input_api_set_error(err, err_size, "`interval_ms` is only valid with `path`.");
        return false;
    }
    if (inputs) {
        return input_api_parse_mouse_buttons(obj, out, err, err_size);
    }
    if (move) {
        return input_api_parse_mouse_move(obj, out, err, err_size);
    }
    if (wheel) {
        return input_api_parse_mouse_wheel(obj, out, err, err_size);
    }
    return input_api_parse_mouse_path(obj, out, err, err_size);
}

static inline bool input_api_parse_release_all_event(JSON_Object *obj, InputParsedEvent &out,
    char *err, size_t err_size)
{
    static const char *const allowed[] = { "kind" };
    if (!input_api_reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err, err_size)) {
        return false;
    }
    if (input_api_has_key(obj, "port") || input_api_has_key(obj, "inputs") || input_api_has_key(obj, "transition")) {
        input_api_set_error(err, err_size, "`release_all` events may only contain `kind`.");
        return false;
    }
    out.kind = INPUT_PARSED_RELEASE_ALL;
    return true;
}

static inline bool input_api_parse_event(JSON *json, InputParsedEvent &out, char *err, size_t err_size)
{
    memset(&out, 0, sizeof(out));
    if (!json || (json->type() != eObject)) {
        input_api_set_error(err, err_size, "Each event must be an object.");
        return false;
    }
    JSON_Object *obj = (JSON_Object *)json;
    const char *kind;
    if (!input_api_get_string_field(obj, "kind", &kind, err, err_size)) {
        return false;
    }
    if (strcmp(kind, "keyboard") == 0) {
        return input_api_parse_keyboard_event(obj, out, err, err_size);
    }
    if (strcmp(kind, "joystick") == 0) {
        return input_api_parse_joystick_event(obj, out, err, err_size);
    }
    if (strcmp(kind, "mouse") == 0) {
        return input_api_parse_mouse_event(obj, out, err, err_size);
    }
    if (strcmp(kind, "release_all") == 0) {
        return input_api_parse_release_all_event(obj, out, err, err_size);
    }
    input_api_set_error(err, err_size, "`kind` must be one of `keyboard`, `joystick`, `mouse`, or `release_all`.");
    return false;
}

static inline bool input_api_validate_batch(JSON *root, InputParsedEvent events[INPUT_API_MAX_EVENTS], int &event_count,
    int &error_index, char *err, size_t err_size)
{
    error_index = -1;
    if (!root || (root->type() != eObject)) {
        input_api_set_error(err, err_size, "Root must be an object.");
        return false;
    }
    JSON_Object *obj = (JSON_Object *)root;
    static const char *const allowed[] = { "events" };
    if (!input_api_reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err, err_size)) {
        return false;
    }
    JSON *events_json = obj->get("events");
    if (!events_json) {
        input_api_set_error(err, err_size, "`events` is required.");
        return false;
    }
    if (events_json->type() != eList) {
        input_api_set_error(err, err_size, "`events` must be an array.");
        return false;
    }
    JSON_List *list = (JSON_List *)events_json;
    event_count = list->get_num_elements();
    if ((event_count < 1) || (event_count > INPUT_API_MAX_EVENTS)) {
        input_api_set_error(err, err_size, "`events` must contain 1..64 entries.");
        return false;
    }
    for (int i = 0; i < event_count; i++) {
        if (!input_api_parse_event((*list)[i], events[i], err, err_size)) {
            error_index = i;
            return false;
        }
    }
    return true;
}

#endif
