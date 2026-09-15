#include "input_api.h"
#include "rest_mouse_queue.h"
#include "route_input_menu.h"

#include "routes.h"
#include "attachment_writer.h"
#include "itu.h"
#include "keyboard_usb.h"
#include "joystick_output.h"
#if U64
#include "usb_hid.h"
#endif

#include "FreeRTOS.h"
#include "semphr.h"
#include "timers.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *INPUT_CAPABILITY_ERROR = "Keyboard and joystick injection require Ultimate 64-class hardware.";
extern "C" bool userinterface_any_menu_active(void) __attribute__((weak));

#if U64
static volatile TickType_t rest_input_menu_button_tick = 0;

extern "C" void route_input_note_menu_button(void)
{
    rest_input_menu_button_tick = xTaskGetTickCount();
}
#endif

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
        case eAbort:
            // Connection torn down before the body finished: free the request
            // args and this writer without running the incomplete API call.
            delete args;
            delete this;
            break;
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
        if (!req->BodyCB) {
            // setup_multipart out of memory: no body callback installed, so free
            // the writer here and report "no body" (execute_api_v1 frees args).
            delete writer;
            return NULL;
        }
        return writer;
    }
    return NULL;
}

}

#if U64

static const uint8_t REST_JOYSTICK_TAP_HOLD_TICKS = 1;
static const TickType_t REST_KEYBOARD_TAP_SETUP_TICKS = 0;
static const TickType_t REST_KEYBOARD_TAP_HOLD_TICKS = (pdMS_TO_TICKS(60) > 0) ? pdMS_TO_TICKS(60) : 1;
static const TickType_t REST_KEYBOARD_TAP_RELEASE_TICKS = 0;
static const TickType_t REST_KEYBOARD_TAP_GAP_TICKS = (pdMS_TO_TICKS(40) > 0) ? pdMS_TO_TICKS(40) : 1;
// How long a queued tap holds its keys down, in ticks of the 20ms REST input
// timer (keyboard_usb.cc REST_INPUT_TIMER_TICKS). The C64 KERNAL scans the
// keyboard once per video frame, so two ticks put the key in front of at least
// two scans, and the one-tick gap that follows (REST_TAP_GAP_TICKS) puts the
// release in front of at least one. Together they set the rate at which
// batched keys cross the matrix: 3+2 ticks measured 100ms a key, 2+1 measured
// 60.7ms a key with nothing lost. See tests/e2e/doc/key-injection-rate.md.
static const uint8_t REST_KEYBOARD_TAP_HOLD_QUEUE_TICKS = 2;
static const int ROUTE_INPUT_MENU_MAX_PENDING_REPEAT_KEYS = 2;
static const TickType_t ROUTE_INPUT_MENU_REPEAT_TIMER_TICKS = (pdMS_TO_TICKS(20) > 0) ? pdMS_TO_TICKS(20) : 1;
static SemaphoreHandle_t rest_input_mutex = NULL;
static TimerHandle_t rest_menu_repeat_timer = NULL;
static RouteInputMenuKeyboardState rest_menu_keyboard_state;
// A mouse tap holds its buttons for two PAL frames, so a program that reads the
// buttons once per frame sees the press.
static const int REST_MOUSE_TAP_HOLD_TICKS = (pdMS_TO_TICKS(40) > 0) ? pdMS_TO_TICKS(40) : 1;
static const TickType_t REST_MOUSE_TIMER_TICKS = 1;
// The shortest time between two REST mouse reports: the 20ms at which the
// firmware polls a USB mouse. See RestMouseQueue.
static const int REST_MOUSE_REPORT_TICKS = (pdMS_TO_TICKS(20) > 0) ? pdMS_TO_TICKS(20) : 1;
static RestMouseQueue rest_mouse_queue;
static TimerHandle_t rest_mouse_timer = NULL;
static TickType_t rest_mouse_last_report = 0;

static void route_input_menu_repeat_timer_callback(TimerHandle_t timer);

struct RouteInputResponseKeys {
    uint8_t matrix[8];
    bool restore;

    void clear(void)
    {
        memset(matrix, 0, sizeof(matrix));
        restore = false;
    }

    void addKeyboardTap(const InputParsedEvent &event)
    {
        const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
        for (int i = 0; i < event.keyboard_count; i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
            if (entry.restore) {
                restore = true;
            } else {
                matrix[entry.row] |= (1 << entry.col);
            }
        }
    }
};

static void ensure_menu_repeat_timer(void)
{
    if (rest_menu_repeat_timer) {
        return;
    }

    TimerHandle_t timer = xTimerCreate("RestMenu", ROUTE_INPUT_MENU_REPEAT_TIMER_TICKS,
        pdTRUE, NULL, route_input_menu_repeat_timer_callback);
    if (!timer) {
        return;
    }

    bool use_timer = false;
    taskENTER_CRITICAL();
    if (!rest_menu_repeat_timer) {
        rest_menu_repeat_timer = timer;
        use_timer = true;
    }
    taskEXIT_CRITICAL();

    if (use_timer) {
        xTimerStart(timer, 0);
    } else {
        xTimerDelete(timer, 0);
    }
}

static SemaphoreHandle_t input_mutex(void)
{
    if (!rest_input_mutex) {
        taskENTER_CRITICAL();
        if (!rest_input_mutex) {
            rest_input_mutex = xSemaphoreCreateMutex();
        }
        taskEXIT_CRITICAL();
    }
    ensure_menu_repeat_timer();
    return rest_input_mutex;
}

// Sends the reports that are due to the REST mouse. It runs in the timer task
// and shares the input mutex with the handlers, so the virtual mouse is only
// ever driven from one place at a time; a tick that finds the mutex taken
// leaves the work to the next tick.
static void rest_mouse_timer_callback(TimerHandle_t timer)
{
    if (!rest_input_mutex || (xSemaphoreTake(rest_input_mutex, 0) != pdTRUE)) {
        return;
    }
    UsbHidDriver *mouse = UsbHidDriver::restMouse();
    TickType_t now = xTaskGetTickCount();
    RestMouseReport report;
    // A report waits while the POT lines still carry earlier movement, which
    // the pacer would otherwise cap (MousePotPacer::MAX_BACKLOG).
    while (mouse && JoystickOutput::instance().mouseSettled() &&
           rest_mouse_queue.takeDue((int)(now - rest_mouse_last_report), REST_MOUSE_REPORT_TICKS, report)) {
        mouse->restMouseReport(report.buttons, report.dx, report.dy, report.wheel, report.pan);
        rest_mouse_last_report = now;
    }
    if (!rest_mouse_queue.pending()) {
        xTimerStop(timer, 0);
    }
    xSemaphoreGive(rest_input_mutex);
}

static void apply_mouse_event(const InputParsedEvent &event)
{
    UsbHidDriver *mouse = UsbHidDriver::restMouse();
    if (!mouse) {
        return;
    }
    if (!rest_mouse_timer) {
        rest_mouse_timer = xTimerCreate("RestMouse", REST_MOUSE_TIMER_TICKS, pdTRUE, NULL, rest_mouse_timer_callback);
        if (!rest_mouse_timer) {
            return;
        }
    }
    if (!mouse->restMouseAttached()) {
        mouse->restMouseAttach();
        rest_mouse_queue.clear(0);
    }
    // An idle queue sends its first report straight away, unless the previous
    // report went out less than 20ms ago. Only a long idle spell is clamped,
    // so the tick difference cannot overflow.
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - rest_mouse_last_report) > (TickType_t)RestMouseQueue::CAPACITY) {
        rest_mouse_last_report = now - RestMouseQueue::CAPACITY;
    }
    rest_mouse_queue.append(event, REST_MOUSE_TAP_HOLD_TICKS, portTICK_PERIOD_MS);
    xTimerStart(rest_mouse_timer, 0);
}

// Drops every pending mouse report, lets go of the buttons and detaches the REST
// mouse, so port 1's POT lines go back to joystick use. Called with the input
// mutex held.
static void release_rest_mouse(void)
{
    rest_mouse_queue.clear(0);
    UsbHidDriver *mouse = UsbHidDriver::restMouse();
    if (mouse && mouse->restMouseAttached()) {
        mouse->restMouseDetach();
    }
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
            // Cancel any in-flight tap so it can't mask this release.
            JoystickOutput::instance().cancelRestPort1Overlay(event.joystick_mask);
        } else {
            JoystickOutput::instance().setRestPort2Persistent(active_low);
            JoystickOutput::instance().cancelRestPort2Overlay(event.joystick_mask);
        }
        break;
    case INPUT_PARSED_TAP:
        for (int i = 0; i < INPUT_API_MAX_JOYSTICK_INPUTS; i++) {
            if (event.joystick_mask & (1 << i)) {
                hold[i] = REST_JOYSTICK_TAP_HOLD_TICKS;
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

static void route_input_queue_menu_repeat_key(uint8_t key)
{
    int opposite = 0;
    switch (key) {
    case KEY_UP:
        opposite = KEY_DOWN;
        break;
    case KEY_DOWN:
        opposite = KEY_UP;
        break;
    case KEY_LEFT:
        opposite = KEY_RIGHT;
        break;
    case KEY_RIGHT:
        opposite = KEY_LEFT;
        break;
    default:
        break;
    }
    if (opposite != 0) {
        system_usb_keyboard.remove_injected_key(opposite);
    }
    int pending = system_usb_keyboard.count_injected_key(key);
    if (pending >= ROUTE_INPUT_MENU_MAX_PENDING_REPEAT_KEYS) {
        return;
    }
    system_usb_keyboard.push_head(key);
}

static bool route_input_menu_active(void)
{
    bool rest_opened_menu = (rest_input_menu_button_tick != 0) &&
        ((xTaskGetTickCount() - rest_input_menu_button_tick) < pdMS_TO_TICKS(60000));
    if (rest_opened_menu && userinterface_any_menu_active && userinterface_any_menu_active()) {
        return true;
    }
    return !system_usb_keyboard.isMatrixEnabled();
}

static bool sync_menu_keyboard_state(void)
{
    bool active = route_input_menu_active();
    rest_menu_keyboard_state.sync(active);
    return active;
}

static void apply_keyboard_menu_event(const InputParsedEvent &event)
{
    uint8_t keys[INPUT_API_MAX_KEYBOARD_INPUTS];
    uint8_t release_keys[INPUT_API_MAX_KEYBOARD_INPUTS];
    int release_key_count = rest_menu_keyboard_state.collectReleaseKeys(event,
        release_keys, INPUT_API_MAX_KEYBOARD_INPUTS);
    int key_count = rest_menu_keyboard_state.applyKeyboardEvent(event, keys,
        INPUT_API_MAX_KEYBOARD_INPUTS, system_usb_keyboard.initialRepeatDelay());
    for (int i = 0; i < release_key_count; i++) {
        system_usb_keyboard.remove_injected_key(release_keys[i]);
    }
    for (int i = 0; i < key_count; i++) {
        system_usb_keyboard.push_head(keys[i]);
    }
}

static void apply_menu_keyboard_repeat(void)
{
    uint8_t keys[1];
    int key_count = rest_menu_keyboard_state.repeatTick(keys, 1, system_usb_keyboard.repeatSpeed());
    for (int i = 0; i < key_count; i++) {
        route_input_queue_menu_repeat_key(keys[i]);
    }
}

static void route_input_menu_repeat_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    SemaphoreHandle_t mutex = input_mutex();
    if (!mutex) {
        return;
    }
    if (xSemaphoreTake(mutex, 0) != pdTRUE) {
        return;
    }
    if (sync_menu_keyboard_state()) {
        apply_menu_keyboard_repeat();
    }
    xSemaphoreGive(mutex);
}

static void release_live_keyboard_event(const InputParsedEvent &event)
{
    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
    uint8_t release_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    for (int i = 0; i < event.keyboard_count; i++) {
        const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
        if (!entry.restore) {
            release_matrix[entry.row] |= (1 << entry.col);
        }
    }
    system_usb_keyboard.restReleaseMatrix(release_matrix);
}

static bool keyboard_matrix_has_any(const uint8_t matrix[8])
{
    for (int row = 0; row < 8; row++) {
        if (matrix[row] != 0) {
            return true;
        }
    }
    return false;
}

static void tap_live_keyboard_matrix(const uint8_t tap_matrix[8], const uint8_t setup_matrix[8], bool restore)
{
	uint8_t release_matrix[8];
	uint8_t persistent_matrix[8];
	bool persistent_restore;
	bool has_tap = keyboard_matrix_has_any(tap_matrix);
	bool has_setup = keyboard_matrix_has_any(setup_matrix);

	system_usb_keyboard.restPersistentSnapshot(persistent_matrix, persistent_restore);
	system_usb_keyboard.restBeginMatrixControl();
	if (has_setup) {
		system_usb_keyboard.restPressMatrix(setup_matrix);
		if (REST_KEYBOARD_TAP_SETUP_TICKS > 0) {
			vTaskDelay(REST_KEYBOARD_TAP_SETUP_TICKS);
		}
    }

    if (has_tap) {
        system_usb_keyboard.restPressMatrix(tap_matrix);
    }
    if (restore) {
        system_usb_keyboard.restPressRestore();
    }

    vTaskDelay(REST_KEYBOARD_TAP_HOLD_TICKS);

    for (int row = 0; row < 8; row++) {
        release_matrix[row] = tap_matrix[row] & ~setup_matrix[row] & ~persistent_matrix[row];
    }
    if (keyboard_matrix_has_any(release_matrix)) {
        system_usb_keyboard.restReleaseMatrix(release_matrix);
    }
    if (restore && !persistent_restore) {
        system_usb_keyboard.restReleaseRestore();
    }

    if (has_setup) {
        if (REST_KEYBOARD_TAP_RELEASE_TICKS > 0) {
            vTaskDelay(REST_KEYBOARD_TAP_RELEASE_TICKS);
        }
        for (int row = 0; row < 8; row++) {
            release_matrix[row] = setup_matrix[row] & ~persistent_matrix[row];
        }
        if (keyboard_matrix_has_any(release_matrix)) {
            system_usb_keyboard.restReleaseMatrix(release_matrix);
        }
	}

	vTaskDelay(REST_KEYBOARD_TAP_GAP_TICKS);
	system_usb_keyboard.restEndMatrixControl();
}

static void apply_keyboard_event(const InputParsedEvent &event, RouteInputResponseKeys *response_keys)
{
    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
    uint8_t tap_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t tap_setup_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t live_matrix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    bool tap_restore = false;
    bool menu_active = sync_menu_keyboard_state();

    if (event.transition == INPUT_PARSED_TAP) {
        if (menu_active) {
            apply_keyboard_menu_event(event);
            if (response_keys) {
                response_keys->addKeyboardTap(event);
            }
            return;
        }
        for (int i = 0; i < event.keyboard_count; i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
            if (entry.restore) {
                tap_restore = true;
            } else {
                uint8_t bit = (1 << entry.col);
                tap_matrix[entry.row] |= bit;
            }
        }
        while (!system_usb_keyboard.restQueueTap(tap_matrix, tap_setup_matrix,
            tap_restore, REST_KEYBOARD_TAP_HOLD_QUEUE_TICKS)) {
            vTaskDelay(1);
        }
        if (response_keys) {
            response_keys->addKeyboardTap(event);
        }
        return;
    }

    if (event.transition == INPUT_PARSED_RELEASE) {
        // A live-matrix press can be released during a brief UI/menu ownership
        // handoff. Releases are cleanup, so clear the live REST matrix even
        // when the same event is also routed to menu-held state below.
        release_live_keyboard_event(event);
    }

    if (menu_active) {
        apply_keyboard_menu_event(event);
        return;
    }

    if (event.transition == INPUT_PARSED_PRESS) {
        for (int i = 0; i < event.keyboard_count; i++) {
            const InputKeyboardMapEntry &entry = keyboard_map[event.keyboard_index[i]];
            if (!entry.restore) {
                live_matrix[entry.row] |= (1 << entry.col);
            }
        }
        system_usb_keyboard.restPressMatrix(live_matrix);
    }
}

static void apply_batch(const InputParsedEvent *events, int event_count, RouteInputResponseKeys *response_keys)
{
    for (int i = 0; i < event_count; i++) {
        switch (events[i].kind) {
        case INPUT_PARSED_KEYBOARD:
            apply_keyboard_event(events[i], response_keys);
            break;
        case INPUT_PARSED_JOYSTICK:
            apply_joystick_event(events[i]);
            break;
        case INPUT_PARSED_MOUSE_BUTTONS:
        case INPUT_PARSED_MOUSE_MOVE:
        case INPUT_PARSED_MOUSE_WHEEL:
        case INPUT_PARSED_MOUSE_PATH:
            apply_mouse_event(events[i]);
            break;
        case INPUT_PARSED_RELEASE_ALL:
            rest_input_menu_button_tick = 0;
            rest_menu_keyboard_state.clear();
            if (response_keys) {
                response_keys->clear();
            }
            system_usb_keyboard.restReleaseAll();
            JoystickOutput::instance().releaseAllRest();
            release_rest_mouse();
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

static void emit_state_snapshot(ResponseWrapper *resp, const RouteInputResponseKeys *response_keys)
{
    uint8_t matrix[8];
    bool restore;
    uint8_t joy1;
    uint8_t joy2;
    uint8_t menu_matrix[8];

    sync_menu_keyboard_state();
    system_usb_keyboard.restSnapshot(matrix, restore);
    JoystickOutput::instance().snapshot(joy1, joy2);
    rest_menu_keyboard_state.snapshot(menu_matrix);
    if (response_keys) {
        for (int i = 0; i < 8; i++) {
            matrix[i] |= response_keys->matrix[i];
        }
        restore = restore || response_keys->restore;
    }

    const InputKeyboardMapEntry *keyboard_map = input_api_keyboard_map();
    JSON_List *keyboard_inputs = JSON::List();
    for (int i = 0; i < input_api_keyboard_map_count(); i++) {
        const InputKeyboardMapEntry &entry = keyboard_map[i];
        if (entry.restore) {
            if (restore) {
                keyboard_inputs->add(entry.name);
            }
        } else if ((matrix[entry.row] & (1 << entry.col)) || (menu_matrix[entry.row] & (1 << entry.col))) {
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

    UsbHidDriver *mouse = UsbHidDriver::restMouse();
    bool attached = mouse && mouse->restMouseAttached();
    uint8_t buttons = attached ? mouse->restMouseButtons() : 0;
    JSON_List *mouse_inputs = JSON::List();
    for (int i = 0; i < INPUT_API_MOUSE_BUTTON_MAP_COUNT; i++) {
        if (buttons & INPUT_API_MOUSE_BUTTON_MAP[i].mask) {
            mouse_inputs->add(INPUT_API_MOUSE_BUTTON_MAP[i].name);
        }
    }
    resp->json->add("mouse", JSON::Obj()
        ->add("attached", attached)
        ->add("inputs", mouse_inputs)
        ->add("pending", rest_mouse_queue.pending()));
}

extern "C" void route_input_release_rest_mouse(void)
{
    // No input request has run yet, so there is no REST mouse to release.
    SemaphoreHandle_t mutex = rest_input_mutex;
    if (!mutex) {
        return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    release_rest_mouse();
    xSemaphoreGive(mutex);
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

// The blocks are split the same way the handlers below are: on a cartridge the
// active body is the 501 branch, so the call has no success to describe.
#if U64
API_DOC(GET, machine, input,
    TAG("Input")
    SUMMARY("Read the keyboard, joystick and mouse state")
    DESCRIPTION("Returns every key and joystick input that is currently held, both the ones this "
                "API is holding and the ones the Ultimate menu is holding, so the answer matches "
                "what the machine sees.\n"
                "\n"
                "`mouse` describes the mouse this API drives: whether it is attached, the buttons "
                "it holds now, and how many of its reports are still waiting to be sent. A client "
                "that needs a path or a wheel turn to have finished waits for `pending` to reach 0.\n"
                "\n"
                "The FPGA build has to carry the block that drives the keyboard and joystick "
                "lines. A build without it answers 501.")
    PATH("/v1/machine:input", "getInputState", "")
    RESPONSE("200", "application/json", "InputStateResponse", "What is being held right now.", "")
    RESPONSE_EXAMPLE("200", "Shift, fire and a mouse button", "{\n  \"keyboard\" : { \"inputs\" : [ \"left_shift\", \"a\" ] },\n  \"joysticks\" : [\n    { \"port\" : 1, \"inputs\" : [ \"up\", \"fire\" ] },\n    { \"port\" : 2, \"inputs\" : [] }\n  ],\n  \"mouse\" : { \"attached\" : true, \"inputs\" : [ \"left\" ], \"pending\" : 0 },\n  \"errors\" : []\n}", "")
    RESPONSE_ERROR("501", "Keyboard and joystick injection require Ultimate 64-class hardware.", "")
    RESPONSE_ERROR("500", "Could not create REST input mutex.", "")
)
#else
API_DOC(GET, machine, input,
    TAG("Input")
    SUMMARY("Read the keyboard, joystick and mouse state")
    DESCRIPTION("Not implemented on this product. Keyboard and joystick injection needs the "
                "hardware that drives those lines, which a cartridge does not have. The call is "
                "registered so that a client is told why rather than reading a 404 as a wrong URL, "
                "and it always answers 501 here. The Ultimate 64 document describes what it does "
                "on hardware that has it.")
    PATH("/v1/machine:input", "getInputState", "")
    RESPONSE_ERROR("501", "Keyboard and joystick injection require Ultimate 64-class hardware.", "")
)
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
    emit_state_snapshot(resp, 0);
    xSemaphoreGive(mutex);
    resp->json_response(HTTP_OK);
#else
    resp->error(INPUT_CAPABILITY_ERROR);
    resp->json_response(HTTP_NOT_IMPLEMENTED);
#endif
}

#if U64
API_DOC(POST, machine, input,
    TAG("Input")
    SUMMARY("Apply keyboard, joystick and mouse events")
    DESCRIPTION("Applies up to 64 input events in the order they are given and returns the state "
                "that results. The whole batch is validated before any of it is applied, so a "
                "batch that is rejected changes nothing.\n"
                "\n"
                "`press` holds an input down until something releases it, `release` lets it go, "
                "and `tap` does both, which is what typing needs. A `release_all` event drops "
                "everything this API is holding, and a machine reset does the same.\n"
                "\n"
                "A `mouse` event drives a wheel mouse on control port 1, which the firmware "
                "handles exactly as it handles a USB mouse: Mouse Mode, the sensitivities, the "
                "Micromys wheel and menu navigation all apply. Each event does one thing. "
                "`inputs` with a `transition` presses, releases or taps `left`, `right` and "
                "`middle`. `move` sends one report of `x` and `y` HID counts, -127 to 127 each, "
                "positive right and down; it is the call to use for the lowest latency. As with a "
                "USB mouse, Mouse Sensitivity scales the counts and one report moves the pointer "
                "by at most 63 on each axis; at the default sensitivity of 8 a count is one "
                "pointer step, so a path that has to arrive exactly keeps its steps within "
                "-63..63. `wheel` "
                "turns `vertical` (positive away from the user) and `horizontal` (positive to "
                "the right) by whole detents, one report per detent. `path` replays up to 256 "
                "`[x, y]` steps, one report every `interval_ms` (20 to 1000, default 20), which "
                "keeps a drawn path's shape; a request holds at most 1024 JSON values and a step "
                "takes three, so one request carries about 330 steps in all. "
                "In Mouse + Wheel mode the Micromys lines queue at most 16 pulses, as for a USB "
                "mouse, and `pending` reaches 0 before the last pulse has ended. Mouse reports are sent in order from a queue, no "
                "closer together than 20ms, the rate at which the firmware polls a USB mouse. A "
                "C64 mouse driver reads the position once per frame and takes more than 63 "
                "counts in one frame as a move the other way, so the firmware spreads fast "
                "movement over several frames, and the next report waits until the movement "
                "before it has reached the port: a fast path takes longer than its `interval_ms` "
                "steps add up to, but no step is lost. So a `release` "
                "after a `path` in the same batch waits for the path, and press, path and "
                "release in one batch is a drag. The response comes back once the batch is "
                "queued; `mouse.pending` says how many reports are still to go. "
                "The first mouse event attaches the mouse and `release_all` detaches it.\n"
                "\n"
                "`restore` is not part of the keyboard matrix; it is wired to NMI, so only "
                "transition `tap` does anything with it. It may share an event with matrix "
                "keys: a tap of `[\"commodore\", \"restore\"]` puts the C= key down in the "
                "matrix before it raises the restore line.\n"
                "\n"
                "The request must be `application/json` and the body must be under 4096 bytes. "
                "The FPGA build has to carry the block that drives those lines; a build without "
                "it answers 501.")
    PATH("/v1/machine:input", "applyInputEvents", "")
    BODY("application/json", "InputBatch", "The events to apply, in order.")
    RESPONSE("200", "application/json", "InputStateResponse", "What is held after the batch was applied.", "")
    RESPONSE_EXAMPLE("200", "After typing LOAD", "{\n  \"keyboard\" : { \"inputs\" : [] },\n  \"joysticks\" : [\n    { \"port\" : 1, \"inputs\" : [] },\n    { \"port\" : 2, \"inputs\" : [] }\n  ],\n  \"mouse\" : { \"attached\" : false, \"inputs\" : [], \"pending\" : 0 },\n  \"errors\" : []\n}", "")
    RESPONSE_ERROR("400", "Content type should be 'application/json'.", "")
    RESPONSE_ERROR("400", "`events` must contain 1..64 entries.", "")
    RESPONSE_ERROR("400", "JSON body is too large.", "")
    RESPONSE_ERROR("400", "events[0]: `kind` must be one of `keyboard`, `joystick`, `mouse`, or `release_all`.", "")
    RESPONSE_ERROR("429", "The batch needs 300 mouse reports and the queue has room for 24; wait for `mouse.pending` to fall.", "")
    RESPONSE_ERROR("501", "Keyboard and joystick injection require Ultimate 64-class hardware.", "")
)
#else
API_DOC(POST, machine, input,
    TAG("Input")
    SUMMARY("Apply keyboard, joystick and mouse events")
    DESCRIPTION("Not implemented on this product. Keyboard and joystick injection needs the "
                "hardware that drives those lines, which a cartridge does not have. The call is "
                "registered so that a client is told why rather than reading a 404 as a wrong URL, "
                "and it always answers 501 here. The Ultimate 64 document describes what it does "
                "on hardware that has it.")
    PATH("/v1/machine:input", "applyInputEvents", "")
    BODY("application/json", "InputBatch", "The events to apply, in order.")
    RESPONSE_ERROR("501", "Keyboard and joystick injection require Ultimate 64-class hardware.", "")
)
#endif
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

    int tokens = convert_text_to_json_objects(text, text_size, INPUT_API_MAX_JSON_TOKENS, &obj);
    if (tokens == -1) {             // JSMN_ERROR_NOMEM: the token budget ran out
        resp->error("The request has more than %d JSON values; send long mouse paths in several requests.",
                    INPUT_API_MAX_JSON_TOKENS);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
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
    int mouse_needed = 0;
    int mouse_room = 0;
    if (!RestMouseQueue::batchFits(events, event_count, rest_mouse_queue.room(), mouse_needed, mouse_room)) {
        xSemaphoreGive(mutex);
        delete obj;
        resp->error("The batch needs %d mouse reports and the queue has room for %d; "
                    "wait for `mouse.pending` to fall.", mouse_needed, mouse_room);
        resp->json_response(HTTP_TOO_MANY_REQUESTS);
        return;
    }
    RouteInputResponseKeys response_keys;
    response_keys.clear();
    apply_batch(events, event_count, &response_keys);
    emit_state_snapshot(resp, &response_keys);
    xSemaphoreGive(mutex);
    resp->json_response(HTTP_OK);
    delete obj;
#else
    resp->error(INPUT_CAPABILITY_ERROR);
    resp->json_response(HTTP_NOT_IMPLEMENTED);
#endif
}
