#include "routes.h"
#include "attachment_writer.h"
#include "input_api.h"
#include "itu.h"
#include "keyboard_usb.h"
#include "joystick_output.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *INPUT_CAPABILITY_ERROR = "Keyboard and joystick injection require Ultimate 64-class hardware.";

#if U64

static const uint8_t REST_TAP_HOLD_TICKS = 1;
static SemaphoreHandle_t rest_input_mutex = NULL;

static SemaphoreHandle_t input_mutex(void)
{
    if (!rest_input_mutex) {
        rest_input_mutex = xSemaphoreCreateMutex();
    }
    return rest_input_mutex;
}

static void apply_joystick_event(const InputParsedEvent &event)
{
    uint8_t p1, p2;
    uint8_t active_low;
    uint8_t hold[INPUT_API_MAX_JOYSTICK_INPUTS] = { 0, 0, 0, 0, 0 };

    JoystickOutput::instance().restPersistentSnapshot(p1, p2);
    active_low = (event.port == 1) ? p1 : p2;

    switch (event.transition) {
    case INPUT_PARSED_PRESS:
        active_low &= ~event.joystick_mask;
        if (event.port == 1) {
            JoystickOutput::instance().setRestPort1Persistent(active_low);
        } else {
            JoystickOutput::instance().setRestPort2Persistent(active_low);
        }
        break;
    case INPUT_PARSED_RELEASE:
        active_low |= event.joystick_mask;
        if (event.port == 1) {
            JoystickOutput::instance().setRestPort1Persistent(active_low);
        } else {
            JoystickOutput::instance().setRestPort2Persistent(active_low);
        }
        break;
    case INPUT_PARSED_TAP:
        for (int i = 0; i < INPUT_API_MAX_JOYSTICK_INPUTS; i++) {
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

static void apply_keyboard_event(const InputParsedEvent &event)
{
    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();

    for (int i = 0; i < event.keyboard_count; i++) {
        const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
        if (entry.restore) {
            system_usb_keyboard.restTapRestore(REST_TAP_HOLD_TICKS);
            continue;
        }
        switch (event.transition) {
        case INPUT_PARSED_PRESS:
            system_usb_keyboard.restPress(entry.row, entry.col);
            break;
        case INPUT_PARSED_RELEASE:
            system_usb_keyboard.restRelease(entry.row, entry.col);
            break;
        case INPUT_PARSED_TAP:
            system_usb_keyboard.restTap(entry.row, entry.col, REST_TAP_HOLD_TICKS);
            break;
        }
    }
}

static void apply_batch(const InputParsedEvent *events, int event_count, int pace_ms)
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
        case INPUT_PARSED_KEYBOARD:
            apply_keyboard_event(events[i]);
            break;
        case INPUT_PARSED_JOYSTICK:
            apply_joystick_event(events[i]);
            break;
        case INPUT_PARSED_RELEASE_ALL:
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
    const InputJoystickMapEntry *joystick_map = input_api_joystick_map();

    for (int i = 0; i < input_api_joystick_map_count(); i++) {
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

    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
    JSON_List *keyboard_inputs = JSON::List();
    for (int i = 0; i < input_api_keyboard_map_count(); i++) {
        const InputKeyboardMapEntry &entry = keyboard_map[i];
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

    static InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int pace_ms = 0;
    int error_index = -1;
    char err[INPUT_API_ERROR_SIZE];
    SemaphoreHandle_t mutex = input_mutex();
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!input_api_validate_batch(obj, events, event_count, pace_ms, error_index, err, sizeof(err))) {
        xSemaphoreGive(mutex);
        delete obj;
        free(text);
        if (error_index >= 0) {
            resp->error("events[%d]: %s", error_index, err);
        } else {
            resp->error("%s", err);
        }
        resp->json_response(HTTP_BAD_REQUEST);
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
