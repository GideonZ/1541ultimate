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

namespace {

static const size_t INPUT_JSON_BODY_MAX_SIZE = 4096;

class InputJsonWriter
{
    HTTPReqMessage *req;
    HTTPRespMessage *resp;
    const ApiCall_t *func;
    ArgsURI *args;
    char *buffer;
    size_t size;
    size_t capacity;
    int status;

    bool reserve(size_t required)
    {
        if (required > INPUT_JSON_BODY_MAX_SIZE) {
            status = -2;
            return false;
        }
        if (required <= capacity) {
            return true;
        }
        char *grown = (char *)realloc(buffer, required + 1);
        if (!grown) {
            status = -3;
            return false;
        }
        buffer = grown;
        capacity = required;
        return true;
    }

public:
    InputJsonWriter(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args)
        : req(req), resp(resp), func(func), args(args), buffer(NULL), size(0), capacity(0), status(0)
    {
        if ((req->bodyType == eTotalSize) && (req->bodySize > 0)) {
            reserve(req->bodySize);
        }
    }

    ~InputJsonWriter()
    {
        free(buffer);
    }

    static void collect_wrapper(BodyDataBlock_t *block)
    {
        InputJsonWriter *writer = (InputJsonWriter *)block->context;
        writer->collect(block);
    }

    void collect(BodyDataBlock_t *block)
    {
        switch (block->type) {
        case eStart:
            size = 0;
            break;
        case eDataBlock:
            if (status != 0) {
                break;
            }
            if (!reserve(size + block->length)) {
                break;
            }
            memcpy(buffer + size, block->data, block->length);
            size += block->length;
            buffer[size] = 0;
            break;
        case eTerminate:
        {
            ResponseWrapper respw(resp);
            if (args->Validate(*func, &respw) != 0) {
                respw.json_response(HTTP_BAD_REQUEST);
            } else {
                func->proc(*args, req, &respw, this);
            }
            delete args;
            delete this;
            break;
        }
        default:
            break;
        }
    }

    int error(void) const
    {
        return status;
    }

    char *data(void)
    {
        return buffer;
    }

    size_t length(void) const
    {
        return size;
    }
};

void *input_json_writer(HTTPReqMessage *req, HTTPRespMessage *resp, const ApiCall_t *func, ArgsURI *args)
{
    if (req->bodyType != eNoBody) {
        if ((req->bodyType == eTotalSize) && (req->bodySize == 0)) {
            req->bodyType = eNoBody;
            return NULL;
        }
        InputJsonWriter *writer = new InputJsonWriter(req, resp, func, args);
        setup_multipart(req, &InputJsonWriter::collect_wrapper, writer);
        return writer;
    }
    return NULL;
}

}

#if U64

static const uint8_t REST_TAP_HOLD_TICKS = 1;
static SemaphoreHandle_t rest_input_mutex = NULL;

static SemaphoreHandle_t input_mutex(void)
{
    if (!rest_input_mutex) {
        taskENTER_CRITICAL();
        if (!rest_input_mutex) {
            rest_input_mutex = xSemaphoreCreateMutex();
        }
        taskEXIT_CRITICAL();
    }
    return rest_input_mutex;
}

static void apply_joystick_event(const InputParsedEvent &event)
{
    uint8_t p1, p2;
    uint8_t active_low;
    uint8_t hold[INPUT_API_MAX_JOYSTICK_INPUTS] = { 0, 0, 0, 0, 0, 0, 0 };

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
        active_low = ((1 << INPUT_API_MAX_JOYSTICK_INPUTS) - 1) & ~event.joystick_mask;
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
    uint8_t tap_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    bool tap_restore = false;

    if (event.transition == INPUT_PARSED_TAP) {
        for (int i = 0; i < event.keyboard_count; i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
            if (entry.restore) {
                tap_restore = true;
            } else {
                tap_matrix[entry.row] |= (1 << entry.col);
            }
        }
        while (!system_usb_keyboard.restQueueTap(tap_matrix, tap_restore, REST_TAP_HOLD_TICKS)) {
            vTaskDelay(1);
        }
        return;
    }

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
            break;
        }
    }
}

static void apply_batch(const InputParsedEvent *events, int event_count)
{
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

API_CALL(POST, machine, input, &input_json_writer, ARRAY( { }))
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
    InputJsonWriter *handler = (InputJsonWriter *)body;
    if (!handler) {
        resp->error("Request body is required.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    if (handler->error() != 0) {
        if (handler->error() == -2) {
            resp->error("JSON body is too large.");
        } else if (handler->error() == -3) {
            resp->error("Could not allocate JSON buffer.");
            resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
            return;
        } else {
            resp->error("Request body is required.");
        }
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    JSON *obj = NULL;
    size_t text_size = handler->length();
    char *text = handler->data();
    if (!text || (text_size == 0)) {
        resp->error("Request body is required.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    int tokens = convert_text_to_json_objects(text, text_size, 1024, &obj);
    if (tokens < 0) {
        resp->error("JSON could not be parsed. Error: %d", tokens);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }

    static InputParsedEvent events[INPUT_API_MAX_EVENTS];
    int event_count = 0;
    int error_index = -1;
    char err[INPUT_API_ERROR_SIZE];
    SemaphoreHandle_t mutex = input_mutex();
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!input_api_validate_batch(obj, events, event_count, error_index, err, sizeof(err))) {
        xSemaphoreGive(mutex);
        delete obj;
        if (error_index >= 0) {
            resp->error("events[%d]: %s", error_index, err);
        } else {
            resp->error("%s", err);
        }
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    apply_batch(events, event_count);
    emit_state_snapshot(resp);
    xSemaphoreGive(mutex);
    resp->json_response(HTTP_OK);
    delete obj;
#else
    resp->error(INPUT_CAPABILITY_ERROR);
    resp->json_response(HTTP_NOT_IMPLEMENTED);
#endif
}
