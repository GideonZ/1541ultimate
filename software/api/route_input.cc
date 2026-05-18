#include "routes.h"
#include "attachment_writer.h"
#include "itu.h"
#include "keyboard_usb.h"
#include "joystick_output.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *INPUT_CAPABILITY_ERROR = "Keyboard and joystick injection require Ultimate 64-class hardware.";
static const char *INPUT_JOYSTICK_PORT2_ERROR = "Joystick port 2 injection is not supported by this FPGA build.";
static const int INPUT_ERROR_SIZE = 160;

#if U64

static const uint8_t REST_TAP_HOLD_TICKS = 1;
static SemaphoreHandle_t rest_input_mutex = NULL;

enum ParsedKind {
    PARSED_KEYBOARD,
    PARSED_JOYSTICK,
    PARSED_RELEASE_ALL
};

enum ParsedTransition {
    PARSED_PRESS,
    PARSED_RELEASE,
    PARSED_TAP
};

struct KeyboardMapEntry {
    const char *name;
    uint8_t row;
    uint8_t col;
    bool restore;
};

struct JoystickMapEntry {
    const char *name;
    uint8_t bit;
};

struct ParsedEvent {
    ParsedKind kind;
    ParsedTransition transition;
    uint8_t port;
    uint8_t keyboard_count;
    uint8_t keyboard_index[8];
    uint8_t joystick_mask;
};

static const KeyboardMapEntry keyboard_map[] = {
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

static const JoystickMapEntry joystick_map[] = {
    { "up", 0 },
    { "down", 1 },
    { "left", 2 },
    { "right", 3 },
    { "fire", 4 },
};

static SemaphoreHandle_t input_mutex(void)
{
    if (!rest_input_mutex) {
        rest_input_mutex = xSemaphoreCreateMutex();
    }
    return rest_input_mutex;
}

static int keyboard_map_count(void)
{
    return sizeof(keyboard_map) / sizeof(keyboard_map[0]);
}

static int joystick_map_count(void)
{
    return sizeof(joystick_map) / sizeof(joystick_map[0]);
}

static void copy_preview(char *dst, size_t dst_size, const char *src)
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

static void set_error(char *err, const char *msg)
{
    copy_preview(err, INPUT_ERROR_SIZE, msg);
}

static bool has_key(JSON_Object *obj, const char *name)
{
    return obj->get(name) != NULL;
}

static bool key_allowed(const char *key, const char *const *allowed, int allowed_count)
{
    for (int i = 0; i < allowed_count; i++) {
        if (strcmp(key, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool reject_unknown_keys(JSON_Object *obj, const char *const *allowed, int allowed_count, char *err)
{
    IndexedList<const char *> *keys = obj->get_keys();
    for (int i = 0; i < keys->get_elements(); i++) {
        const char *key = (*keys)[i];
        if (!key_allowed(key, allowed, allowed_count)) {
            char preview[32];
            copy_preview(preview, sizeof(preview), key);
            sprintf(err, "Unknown field `%s`.", preview);
            return false;
        }
    }
    return true;
}

static bool get_string_field(JSON_Object *obj, const char *name, const char **out, char *err)
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

static bool parse_transition(JSON_Object *obj, ParsedTransition &transition, char *err)
{
    const char *name;
    if (!get_string_field(obj, "transition", &name, err)) {
        return false;
    }
    if (strcmp(name, "press") == 0) {
        transition = PARSED_PRESS;
        return true;
    }
    if (strcmp(name, "release") == 0) {
        transition = PARSED_RELEASE;
        return true;
    }
    if (strcmp(name, "tap") == 0) {
        transition = PARSED_TAP;
        return true;
    }
    set_error(err, "`transition` must be one of `press`, `release`, or `tap`.");
    return false;
}

static int find_keyboard_input(const char *name)
{
    for (int i = 0; i < keyboard_map_count(); i++) {
        if (strcmp(name, keyboard_map[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_joystick_input(const char *name)
{
    for (int i = 0; i < joystick_map_count(); i++) {
        if (strcmp(name, joystick_map[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool parse_keyboard_event(JSON_Object *obj, ParsedEvent &out, char *err)
{
    static const char *const allowed[] = { "kind", "inputs", "transition" };
    if (!reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err)) {
        return false;
    }
    if (has_key(obj, "port")) {
        set_error(err, "`port` is not allowed for keyboard events.");
        return false;
    }
    if (!parse_transition(obj, out.transition, err)) {
        return false;
    }
    JSON *inputs = obj->get("inputs");
    if (!inputs) {
        set_error(err, "`inputs` is required.");
        return false;
    }
    if (inputs->type() != eList) {
        set_error(err, "`inputs` must be an array.");
        return false;
    }
    JSON_List *list = (JSON_List *)inputs;
    int count = list->get_num_elements();
    if ((count < 1) || (count > 8)) {
        set_error(err, "`inputs` must contain 1..8 entries.");
        return false;
    }

    bool seen[sizeof(keyboard_map) / sizeof(keyboard_map[0])] = { false };
    bool has_restore = false;
    out.kind = PARSED_KEYBOARD;
    out.keyboard_count = count;
    for (int i = 0; i < count; i++) {
        JSON *entry = (*list)[i];
        if (entry->type() != eString) {
            set_error(err, "Keyboard inputs must be strings.");
            return false;
        }
        const char *name = ((JSON_String *)entry)->get_string();
        int index = find_keyboard_input(name);
        if (index < 0) {
            char preview[32];
            copy_preview(preview, sizeof(preview), name);
            sprintf(err, "`%s` is not a valid keyboard input.", preview);
            return false;
        }
        if (seen[index]) {
            char preview[32];
            copy_preview(preview, sizeof(preview), name);
            sprintf(err, "`%s` appears more than once in `inputs`.", preview);
            return false;
        }
        seen[index] = true;
        out.keyboard_index[i] = index;
        if (keyboard_map[index].restore) {
            has_restore = true;
        }
    }

    if (has_restore && ((count != 1) || (out.transition != PARSED_TAP))) {
        set_error(err, "`restore` must appear alone in `inputs` and only with transition `tap`.");
        return false;
    }
    return true;
}

static bool parse_joystick_event(JSON_Object *obj, ParsedEvent &out, char *err)
{
    static const char *const allowed[] = { "kind", "port", "inputs", "transition" };
    if (!reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err)) {
        return false;
    }
    if (!parse_transition(obj, out.transition, err)) {
        return false;
    }
    JSON *port = obj->get("port");
    if (!port) {
        set_error(err, "`port` is required.");
        return false;
    }
    if (port->type() != eInteger) {
        set_error(err, "`port` must be an integer.");
        return false;
    }
    int port_value = ((JSON_Integer *)port)->get_value();
    if ((port_value != 1) && (port_value != 2)) {
        set_error(err, "`port` must be 1 or 2.");
        return false;
    }

    JSON *inputs = obj->get("inputs");
    if (!inputs) {
        set_error(err, "`inputs` is required.");
        return false;
    }
    if (inputs->type() != eList) {
        set_error(err, "`inputs` must be an array.");
        return false;
    }
    JSON_List *list = (JSON_List *)inputs;
    int count = list->get_num_elements();
    if ((count < 1) || (count > 5)) {
        set_error(err, "`inputs` must contain 1..5 entries.");
        return false;
    }

    bool seen[sizeof(joystick_map) / sizeof(joystick_map[0])] = { false };
    out.kind = PARSED_JOYSTICK;
    out.port = port_value;
    out.joystick_mask = 0;
    for (int i = 0; i < count; i++) {
        JSON *entry = (*list)[i];
        if (entry->type() != eString) {
            set_error(err, "Joystick inputs must be strings.");
            return false;
        }
        const char *name = ((JSON_String *)entry)->get_string();
        int index = find_joystick_input(name);
        if (index < 0) {
            char preview[32];
            copy_preview(preview, sizeof(preview), name);
            sprintf(err, "`%s` is not a valid joystick input.", preview);
            return false;
        }
        if (seen[index]) {
            char preview[32];
            copy_preview(preview, sizeof(preview), name);
            sprintf(err, "`%s` appears more than once in `inputs`.", preview);
            return false;
        }
        seen[index] = true;
        out.joystick_mask |= (1 << joystick_map[index].bit);
    }
    return true;
}

static bool parse_release_all_event(JSON_Object *obj, ParsedEvent &out, char *err)
{
    static const char *const allowed[] = { "kind" };
    if (!reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err)) {
        return false;
    }
    if (has_key(obj, "port") || has_key(obj, "inputs") || has_key(obj, "transition")) {
        set_error(err, "`release_all` events may only contain `kind`.");
        return false;
    }
    out.kind = PARSED_RELEASE_ALL;
    return true;
}

static bool parse_event(JSON *json, ParsedEvent &out, char *err)
{
    if (json->type() != eObject) {
        set_error(err, "Each event must be an object.");
        return false;
    }
    JSON_Object *obj = (JSON_Object *)json;
    const char *kind;
    if (!get_string_field(obj, "kind", &kind, err)) {
        return false;
    }
    if (strcmp(kind, "keyboard") == 0) {
        return parse_keyboard_event(obj, out, err);
    }
    if (strcmp(kind, "joystick") == 0) {
        return parse_joystick_event(obj, out, err);
    }
    if (strcmp(kind, "release_all") == 0) {
        return parse_release_all_event(obj, out, err);
    }
    set_error(err, "`kind` must be one of `keyboard`, `joystick`, or `release_all`.");
    return false;
}

static bool validate_batch(ResponseWrapper *resp, JSON *root, ParsedEvent events[64], int &event_count, int &pace_ms)
{
    if (root->type() != eObject) {
        resp->error("Root must be an object.");
        return false;
    }
    JSON_Object *obj = (JSON_Object *)root;
    static const char *const allowed[] = { "events", "pace_ms" };
    char err[INPUT_ERROR_SIZE];
    if (!reject_unknown_keys(obj, allowed, sizeof(allowed) / sizeof(allowed[0]), err)) {
        resp->error("%s", err);
        return false;
    }
    pace_ms = 0;
    JSON *pace = obj->get("pace_ms");
    if (pace) {
        if (pace->type() != eInteger) {
            resp->error("`pace_ms` must be an integer.");
            return false;
        }
        pace_ms = ((JSON_Integer *)pace)->get_value();
        if ((pace_ms < 0) || (pace_ms > 1000)) {
            resp->error("`pace_ms` must be between 0 and 1000.");
            return false;
        }
    }
    JSON *events_json = obj->get("events");
    if (!events_json) {
        resp->error("`events` is required.");
        return false;
    }
    if (events_json->type() != eList) {
        resp->error("`events` must be an array.");
        return false;
    }
    JSON_List *list = (JSON_List *)events_json;
    event_count = list->get_num_elements();
    if ((event_count < 1) || (event_count > 64)) {
        resp->error("`events` must contain 1..64 entries.");
        return false;
    }
    for (int i = 0; i < event_count; i++) {
        err[0] = 0;
        if (!parse_event((*list)[i], events[i], err)) {
            resp->error("events[%d]: %s", i, err);
            return false;
        }
    }
    return true;
}

static bool batch_uses_unsupported_port2(const ParsedEvent *events, int event_count)
{
    if (JoystickOutput::port2Supported()) {
        return false;
    }
    for (int i = 0; i < event_count; i++) {
        if ((events[i].kind == PARSED_JOYSTICK) && (events[i].port == 2)) {
            return true;
        }
    }
    return false;
}

static void apply_joystick_event(const ParsedEvent &event)
{
    uint8_t p1, p2;
    uint8_t active_low;
    uint8_t hold[5] = { 0, 0, 0, 0, 0 };

    JoystickOutput::instance().restPersistentSnapshot(p1, p2);
    active_low = (event.port == 1) ? p1 : p2;

    switch (event.transition) {
    case PARSED_PRESS:
        active_low &= ~event.joystick_mask;
        if (event.port == 1) {
            JoystickOutput::instance().setRestPort1Persistent(active_low);
        } else {
            JoystickOutput::instance().setRestPort2Persistent(active_low);
        }
        break;
    case PARSED_RELEASE:
        active_low |= event.joystick_mask;
        if (event.port == 1) {
            JoystickOutput::instance().setRestPort1Persistent(active_low);
        } else {
            JoystickOutput::instance().setRestPort2Persistent(active_low);
        }
        break;
    case PARSED_TAP:
        for (int i = 0; i < 5; i++) {
            if (event.joystick_mask & (1 << i)) {
                hold[i] = REST_TAP_HOLD_TICKS;
            }
        }
        active_low = 0x1F & ~event.joystick_mask;
        if (event.port == 1) {
            JoystickOutput::instance().armRestPort1Overlay(active_low, hold);
        } else {
            JoystickOutput::instance().armRestPort2Overlay(active_low, hold);
        }
        break;
    }
}

static void apply_keyboard_event(const ParsedEvent &event)
{
    for (int i = 0; i < event.keyboard_count; i++) {
        const KeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
        if (entry.restore) {
            system_usb_keyboard.restTapRestore(REST_TAP_HOLD_TICKS);
            continue;
        }
        switch (event.transition) {
        case PARSED_PRESS:
            system_usb_keyboard.restPress(entry.row, entry.col);
            break;
        case PARSED_RELEASE:
            system_usb_keyboard.restRelease(entry.row, entry.col);
            break;
        case PARSED_TAP:
            system_usb_keyboard.restTap(entry.row, entry.col, REST_TAP_HOLD_TICKS);
            break;
        }
    }
}

static void apply_batch(const ParsedEvent *events, int event_count, int pace_ms)
{
    TickType_t delay_ticks = 0;
    if (pace_ms > 0) {
        delay_ticks = pdMS_TO_TICKS(pace_ms);
        if (delay_ticks == 0) {
            delay_ticks = 1;
        }
    }
    for (int i = 0; i < event_count; i++) {
        switch (events[i].kind) {
        case PARSED_KEYBOARD:
            apply_keyboard_event(events[i]);
            break;
        case PARSED_JOYSTICK:
            apply_joystick_event(events[i]);
            break;
        case PARSED_RELEASE_ALL:
            system_usb_keyboard.restReleaseAll();
            JoystickOutput::instance().releaseAllRest();
            break;
        }
        if (delay_ticks && ((i + 1) < event_count)) {
            vTaskDelay(delay_ticks);
        }
    }
}

static void emit_joystick_inputs(JSON_Object *obj, uint8_t active_low)
{
    JSON_List *inputs = JSON::List();
    for (int i = 0; i < joystick_map_count(); i++) {
        uint8_t bit = (1 << joystick_map[i].bit);
        if (!(active_low & bit)) {
            inputs->add(joystick_map[i].name);
        }
    }
    obj->add("inputs", inputs);
}

static void emit_state_snapshot(ResponseWrapper *resp)
{
    uint8_t matrix[8];
    bool restore;
    uint8_t joy1;
    uint8_t joy2;

    system_usb_keyboard.restSnapshot(matrix, restore);
    JoystickOutput::instance().snapshot(joy1, joy2);

    JSON_List *keyboard_inputs = JSON::List();
    for (int i = 0; i < keyboard_map_count(); i++) {
        const KeyboardMapEntry &entry = keyboard_map[i];
        if (entry.restore) {
            if (restore) {
                keyboard_inputs->add(entry.name);
            }
        } else if (matrix[entry.row] & (1 << entry.col)) {
            keyboard_inputs->add(entry.name);
        }
    }

    resp->json->add("keyboard", JSON::Obj()->add("inputs", keyboard_inputs));

    JSON_List *joysticks = JSON::List();
    JSON_Object *port1 = JSON::Obj()->add("port", 1);
    emit_joystick_inputs(port1, joy1);
    joysticks->add(port1);

    JSON_Object *port2 = JSON::Obj()->add("port", 2);
    emit_joystick_inputs(port2, joy2);
    joysticks->add(port2);

    resp->json->add("joysticks", joysticks);
}

static bool ensure_input_capability(ResponseWrapper *resp)
{
    if (!(getFpgaCapabilities() & CAPAB_ULTIMATE64)) {
        resp->error(INPUT_CAPABILITY_ERROR);
        resp->json_response(HTTP_NOT_IMPLEMENTED);
        return false;
    }
    return true;
}

#endif

API_CALL(GET, machine, input, NULL, ARRAY( { }))
{
#if U64
    if (!ensure_input_capability(resp)) {
        return;
    }
    SemaphoreHandle_t mutex = input_mutex();
    if (!mutex) {
        resp->error("Could not create REST input mutex.");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    emit_state_snapshot(resp);
    xSemaphoreGive(mutex);
    resp->json_response(HTTP_OK);
#else
    resp->error(INPUT_CAPABILITY_ERROR);
    resp->json_response(HTTP_NOT_IMPLEMENTED);
#endif
}

API_CALL(POST, machine, input, &attachment_writer, ARRAY( { }))
{
#if U64
    if (!ensure_input_capability(resp)) {
        return;
    }
    if (!input_mutex()) {
        resp->error("Could not create REST input mutex.");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    if (!req->ContentType || (strcasecmp(req->ContentType, "application/json") != 0)) {
        resp->error("Content type should be 'application/json'.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    TempfileWriter *handler = (TempfileWriter *)body;
    if (!handler) {
        resp->error("Request body is required.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    int buffered = handler->buffer_file(0, 4096);
    if (buffered != 0) {
        if (buffered == -1) {
            resp->error("Request body is required.");
        } else if (buffered == -2) {
            resp->error("JSON body is too large.");
        } else {
            resp->error("Could not buffer attachment.");
        }
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    JSON *obj = NULL;
    size_t text_size = handler->get_filesize(0);
    char *text = (char *)malloc(text_size + 1);
    if (!text) {
        resp->error("Could not allocate JSON buffer.");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    memcpy(text, handler->get_buffer(0), text_size);
    text[text_size] = 0;

    int tokens = convert_text_to_json_objects(text, text_size, 1024, &obj);
    if (tokens < 0) {
        resp->error("JSON could not be parsed. Error: %d", tokens);
        free(text);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    static ParsedEvent events[64];
    int event_count = 0;
    int pace_ms = 0;
    SemaphoreHandle_t mutex = input_mutex();
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!validate_batch(resp, obj, events, event_count, pace_ms)) {
        xSemaphoreGive(mutex);
        delete obj;
        free(text);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    if (batch_uses_unsupported_port2(events, event_count)) {
        xSemaphoreGive(mutex);
        delete obj;
        free(text);
        resp->error(INPUT_JOYSTICK_PORT2_ERROR);
        resp->json_response(HTTP_NOT_IMPLEMENTED);
        return;
    }

    apply_batch(events, event_count, pace_ms);
    emit_state_snapshot(resp);
    xSemaphoreGive(mutex);
    resp->json_response(HTTP_OK);
    delete obj;
    free(text);
#else
    resp->error(INPUT_CAPABILITY_ERROR);
    resp->json_response(HTTP_NOT_IMPLEMENTED);
#endif
}
