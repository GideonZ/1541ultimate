#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "itu.h"
#include "integer.h"
#include "usb_hid.h"
#include "usb_hid_config.h"
#include "usb_hid_selection.h"
#include "profiler.h"
#include "FreeRTOS.h"
#include "task.h"
#include "keyboard_usb.h"
#include "u64.h"

// Standards-based HID support only. Devices must expose standard HID mouse or
// keyboard collections; proprietary non-HID control protocols are intentionally
// out of scope for this driver.

enum {
    WHEEL_DIRECTION_NORMAL = 0,
    WHEEL_DIRECTION_REVERSED = 1
};

enum {
    MOUSE_ACCELERATION_OFF = 0,
    MOUSE_ACCELERATION_ADAPTIVE = 1
};

static int usb_hid_get_config_value(int key, int default_value);
extern "C" void u64_refresh_usb_hid_status(void) __attribute__((weak));
extern "C" bool userinterface_any_menu_active(void) __attribute__((weak));

namespace {

enum {
    USB_HID_MAX_KEYBOARD_SOURCES = 8
};

// HID menu navigation is serviced from the USB interrupt path at roughly 50 Hz, so
// fast/slow/reset thresholds are expressed in a few report intervals rather than
// frame time. The reset window is keyed off the last same-direction report so a
// single notch stays collapsed to one menu move without letting late opposite-
// direction cleanup tails create a long user-visible backoff.
static const uint32_t USB_HID_MENU_WHEEL_FAST_GAP_TICKS = (pdMS_TO_TICKS(30) > 0) ? pdMS_TO_TICKS(30) : 1;
static const uint32_t USB_HID_MENU_WHEEL_SLOW_GAP_TICKS = (pdMS_TO_TICKS(90) > 0) ? pdMS_TO_TICKS(90) : USB_HID_MENU_WHEEL_FAST_GAP_TICKS;
static const uint32_t USB_HID_MENU_WHEEL_RESET_GAP_TICKS = (pdMS_TO_TICKS(210) > 0) ? pdMS_TO_TICKS(210) : USB_HID_MENU_WHEEL_SLOW_GAP_TICKS;
static const uint32_t USB_HID_MOUSE_WHEEL_PULSE_TICKS = (pdMS_TO_TICKS(50) > 0) ? pdMS_TO_TICKS(50) : 1;
static const uint8_t USB_HID_MOUSE_WHEEL_BURST_LIMIT = 16;
static const int USB_HID_MENU_WHEEL_EXTRA_STEP_THRESHOLD = 15;
static const int USB_HID_MENU_MAX_PENDING_VERTICAL_KEYS = 2;

int usb_hid_active_mouse_interfaces = 0;

struct t_usb_hid_visibility
{
    char name[33];
    char mode[12];
    const UsbDevice *source_device;
    int source_interface;
};

t_usb_hid_visibility usb_hid_mouse_visibility = { "Not connected", "-", NULL, -1 };
t_usb_hid_visibility usb_hid_keyboard_visibility = { "Not connected", "-", NULL, -1 };

struct t_usb_hid_keyboard_source
{
    bool occupied;
    const UsbDevice *source_device;
    int source_interface;
    uint8_t report[8];
};

t_usb_hid_keyboard_source usb_hid_keyboard_sources[USB_HID_MAX_KEYBOARD_SOURCES] = { 0 };

void usb_hid_clear_visibility(t_usb_hid_visibility& visibility)
{
    portENTER_CRITICAL();
    strcpy(visibility.name, "Not connected");
    strcpy(visibility.mode, "-");
    visibility.source_device = NULL;
    visibility.source_interface = -1;
    portEXIT_CRITICAL();
}

void usb_hid_set_visibility(t_usb_hid_visibility& visibility, UsbDevice *device, UsbInterface *interface, bool report_protocol)
{
    char name_buffer[sizeof(visibility.name)] = { 0 };
    char interface_name[sizeof(visibility.name)] = { 0 };
    const char *name = "Connected";
    if (device) {
        if (interface && interface->getInterfaceDescriptor()->interface_string) {
            device->get_string(interface->getInterfaceDescriptor()->interface_string, interface_name, sizeof(interface_name));
        }
        if (device->manufacturer[0] && device->product[0]) {
            if (strncmp(device->product, device->manufacturer, strlen(device->manufacturer)) == 0) {
                name = device->product;
            } else {
                strncpy(name_buffer, device->manufacturer, sizeof(name_buffer) - 1);
                size_t used = strlen(name_buffer);
                if (used < (sizeof(name_buffer) - 1)) {
                    name_buffer[used++] = ' ';
                    name_buffer[used] = 0;
                }
                if (used < (sizeof(name_buffer) - 1)) {
                    strncpy(name_buffer + used, device->product, sizeof(name_buffer) - 1 - used);
                    name_buffer[sizeof(name_buffer) - 1] = 0;
                }
                name = name_buffer;
            }
        } else if (device->product[0]) {
            name = device->product;
        } else if (device->manufacturer[0]) {
            name = device->manufacturer;
        }

        if (interface_name[0] && strstr(name, "Receiver")) {
            name = interface_name;
        }
    }
    portENTER_CRITICAL();
    strncpy(visibility.name, name, sizeof(visibility.name) - 1);
    visibility.name[sizeof(visibility.name) - 1] = 0;
    strcpy(visibility.mode, report_protocol ? "Reporting" : "Boot");
    visibility.source_device = device;
    visibility.source_interface = interface ? interface->getInterfaceDescriptor()->interface_number : -1;
    portEXIT_CRITICAL();
}

bool usb_hid_menu_mouse_navigation_enabled(void)
{
    return usb_hid_get_config_value(CFG_MENU_MOUSE_NAV, 1) != 0;
}

bool usb_hid_menu_override_active(void)
{
    if (!usb_hid_menu_mouse_navigation_enabled()) {
        return false;
    }
    if (!userinterface_any_menu_active) {
        return false;
    }
    return userinterface_any_menu_active();
}

void usb_hid_publish_visibility(void)
{
    if (u64_refresh_usb_hid_status) {
        u64_refresh_usb_hid_status();
    }
}

int usb_hid_get_source_interface(UsbInterface *interface)
{
    return interface ? interface->getInterfaceDescriptor()->interface_number : -1;
}

void usb_hid_append_boot_key(uint8_t out[8], uint8_t key)
{
    if (key == 0) {
        return;
    }
    for (int i = 2; i < 8; i++) {
        if (out[i] == key) {
            return;
        }
    }
    for (int i = 2; i < 8; i++) {
        if (out[i] == 0) {
            out[i] = key;
            return;
        }
    }
}

t_usb_hid_keyboard_source *usb_hid_find_keyboard_source(UsbDevice *device, UsbInterface *interface, bool create)
{
    const UsbDevice *source_device = device;
    int interface_number = usb_hid_get_source_interface(interface);
    t_usb_hid_keyboard_source *free_slot = NULL;

    for (int i = 0; i < USB_HID_MAX_KEYBOARD_SOURCES; i++) {
        t_usb_hid_keyboard_source *source = &usb_hid_keyboard_sources[i];
        if (source->occupied) {
            if ((source->source_device == source_device) && (source->source_interface == interface_number)) {
                return source;
            }
        } else if (!free_slot) {
            free_slot = source;
        }
    }

    if (!create || !free_slot) {
        return NULL;
    }

    free_slot->occupied = true;
    free_slot->source_device = source_device;
    free_slot->source_interface = interface_number;
    memset(free_slot->report, 0, sizeof(free_slot->report));
    return free_slot;
}

void usb_hid_submit_keyboard_sources(void)
{
    uint8_t combined_report[8] = { 0 };
    for (int i = 0; i < USB_HID_MAX_KEYBOARD_SOURCES; i++) {
        t_usb_hid_keyboard_source *source = &usb_hid_keyboard_sources[i];
        if (!source->occupied) {
            continue;
        }
        combined_report[0] |= source->report[0];
        for (int key_index = 2; key_index < 8; key_index++) {
            usb_hid_append_boot_key(combined_report, source->report[key_index]);
        }
    }
    system_usb_keyboard.process_data(combined_report);
}

void usb_hid_update_keyboard_source(UsbDevice *device, UsbInterface *interface, const uint8_t report[8])
{
    t_usb_hid_keyboard_source *source = usb_hid_find_keyboard_source(device, interface, true);
    if (!source) {
        return;
    }
    memcpy(source->report, report, sizeof(source->report));
    usb_hid_submit_keyboard_sources();
}

void usb_hid_remove_keyboard_source(UsbDevice *device, UsbInterface *interface)
{
    t_usb_hid_keyboard_source *source = usb_hid_find_keyboard_source(device, interface, false);
    if (!source) {
        return;
    }
    source->occupied = false;
    source->source_device = NULL;
    source->source_interface = -1;
    memset(source->report, 0, sizeof(source->report));
    usb_hid_submit_keyboard_sources();
}

bool usb_hid_keyboard_report_has_activity(const uint8_t *report)
{
    if (!report) {
        return false;
    }
    if (report[0] || report[1]) {
        return true;
    }
    for (int i = 2; i < 8; i++) {
        if (report[i]) {
            return true;
        }
    }
    return false;
}

bool usb_hid_mouse_report_has_activity(int motion_x, int motion_y, int wheel_h, int wheel_v, uint8_t mouse_buttons, uint8_t previous_buttons)
{
    return (motion_x != 0) || (motion_y != 0) || (wheel_h != 0) || (wheel_v != 0) || (mouse_buttons != previous_buttons);
}

int usb_hid_filter_menu_wheel_step(int step, int& latch)
{
    if (step == 0) {
        latch = 0;
        return 0;
    }
    if (step == latch) {
        return 0;
    }
    latch = step;
    return step;
}

void usb_hid_clear_visibility_if_source_matches(t_usb_hid_visibility& visibility, UsbDevice *device, UsbInterface *interface)
{
    const UsbDevice *source_device = device;
    int interface_number = interface ? interface->getInterfaceDescriptor()->interface_number : -1;
    if (usb_hid_source_matches(visibility.source_device, visibility.source_interface,
                               source_device, interface_number)) {
        usb_hid_clear_visibility(visibility);
    }
}

void usb_hid_apply_mouse_output_enable()
{
#if U64
    C64_MOUSE_EN_1 = (usb_hid_active_mouse_interfaces > 0) ? 1 : 0;
    if (usb_hid_active_mouse_interfaces == 0) {
        C64_JOY1_SWOUT = 0x1F;
    }
#endif
}

}

void usb_hid_get_status_snapshot(t_usb_hid_status_snapshot& snapshot)
{
    portENTER_CRITICAL();
    memcpy(snapshot.mouse_name, usb_hid_mouse_visibility.name, sizeof(snapshot.mouse_name));
    memcpy(snapshot.mouse_mode, usb_hid_mouse_visibility.mode, sizeof(snapshot.mouse_mode));
    memcpy(snapshot.keyboard_name, usb_hid_keyboard_visibility.name, sizeof(snapshot.keyboard_name));
    memcpy(snapshot.keyboard_mode, usb_hid_keyboard_visibility.mode, sizeof(snapshot.keyboard_mode));
    portEXIT_CRITICAL();
}

extern "C" int u64_get_usb_hid_config_value(int key, int default_value) __attribute__((weak));

static int usb_hid_get_config_value(int key, int default_value)
{
    if (!u64_get_usb_hid_config_value) {
        return default_value;
    }
    return u64_get_usb_hid_config_value(key, default_value);
}

static int usb_hid_get_scroll_factor(void)
{
    return usb_hid_get_config_value(CFG_SCROLL_FACTOR, 8);
}

static int usb_hid_get_mouse_sensitivity(void)
{
    return usb_hid_get_config_value(CFG_MOUSE_SENSITIVITY, 8);
}

static int usb_hid_get_mouse_acceleration(void)
{
    return usb_hid_get_config_value(CFG_MOUSE_ACCELERATION, MOUSE_ACCELERATION_OFF);
}

static int usb_hid_get_mouse_mode(void)
{
    return usb_hid_get_config_value(CFG_MOUSE_MODE, HidMouseInterpreter::MOUSE_MODE_MOUSE);
}

static int usb_hid_get_wheel_direction(void)
{
    return usb_hid_get_config_value(CFG_WHEEL_DIRECTION, WHEEL_DIRECTION_NORMAL);
}

static void usb_hid_apply_pointer_sensitivity(int& motion_x, int& motion_y, int sensitivity,
                                              int& pointer_sensitivity_setting,
                                              int& pointer_sensitivity_remainder_x,
                                              int& pointer_sensitivity_remainder_y)
{
    int raw_motion_x = motion_x;
    int raw_motion_y = motion_y;
    if (pointer_sensitivity_setting != sensitivity) {
        pointer_sensitivity_remainder_x = 0;
        pointer_sensitivity_remainder_y = 0;
        pointer_sensitivity_setting = sensitivity;
    }

    motion_x = HidMouseInterpreter::clampDelta(
        HidMouseInterpreter::scaleFixedWithRemainder(raw_motion_x,
                                                     HidMouseInterpreter::computeSensitivityScaleFactor(sensitivity),
                                                     pointer_sensitivity_remainder_x),
        63);
    motion_y = HidMouseInterpreter::clampDelta(
        HidMouseInterpreter::scaleFixedWithRemainder(raw_motion_y,
                                                     HidMouseInterpreter::computeSensitivityScaleFactor(sensitivity),
                                                     pointer_sensitivity_remainder_y),
        63);
}

static void usb_hid_apply_pointer_acceleration(int& motion_x, int& motion_y,
                                               int raw_motion_x, int raw_motion_y, int acceleration,
                                               int& adaptive_accel_ema_x16,
                                               int& adaptive_accel_scale_factor)
{
    if (acceleration == MOUSE_ACCELERATION_ADAPTIVE) {
        adaptive_accel_ema_x16 = HidMouseInterpreter::updateAdaptiveAccelerationEma(
            adaptive_accel_ema_x16, raw_motion_x, raw_motion_y);
        adaptive_accel_scale_factor = HidMouseInterpreter::limitScaleStep(
            adaptive_accel_scale_factor,
            HidMouseInterpreter::computeAdaptiveAccelerationScale(adaptive_accel_ema_x16),
            32);
        motion_x = HidMouseInterpreter::clampDelta(
            HidMouseInterpreter::scaleFixed(motion_x, adaptive_accel_scale_factor), 63);
        motion_y = HidMouseInterpreter::clampDelta(
            HidMouseInterpreter::scaleFixed(motion_y, adaptive_accel_scale_factor), 63);
        return;
    }

    adaptive_accel_ema_x16 = 0;
    adaptive_accel_scale_factor = HidMouseInterpreter::ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
}

static void usb_hid_set_mouse_button(uint8_t& mouse_joy, uint8_t mask, bool pressed)
{
    if (pressed) {
        mouse_joy &= ~mask;
    } else {
        mouse_joy |= mask;
    }
}

static void usb_hid_reset_native_wheel_state(int& wheel_step_accumulator,
                                             int& wheel_pulse_phase,
                                             uint8_t& wheel_pulse_mask,
                                             uint32_t& wheel_pulse_next_tick,
                                             int& wheel_pulse_burst_direction,
                                             uint8_t& wheel_pulse_burst_count)
{
    wheel_step_accumulator = 0;
    wheel_pulse_phase = HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE;
    wheel_pulse_mask = 0;
    wheel_pulse_next_tick = 0;
    wheel_pulse_burst_direction = 0;
    wheel_pulse_burst_count = 0;
}

static uint8_t usb_hid_service_native_wheel(uint8_t mouse_joy,
                                            int& wheel_pulse_burst_direction,
                                            uint8_t& wheel_pulse_burst_count,
                                            int& wheel_pulse_phase,
                                            uint8_t& wheel_pulse_mask,
                                            uint32_t& wheel_pulse_next_tick,
                                            uint32_t now_ticks)
{
    HidMouseInterpreter::advanceNativeWheelBurst(wheel_pulse_burst_direction,
                                                 wheel_pulse_burst_count,
                                                 wheel_pulse_phase,
                                                 wheel_pulse_mask,
                                                 wheel_pulse_next_tick,
                                                 now_ticks,
                                                 USB_HID_MOUSE_WHEEL_PULSE_TICKS);

    return HidMouseInterpreter::applyWheelPulseMask(mouse_joy,
                                                    wheel_pulse_phase,
                                                    wheel_pulse_mask);
}

static void usb_hid_publish_native_wheel_input(int *pending_queue,
                                               uint8_t queue_size,
                                               uint8_t& queue_head,
                                               uint8_t& queue_tail,
                                               uint8_t& base_joy,
                                               int wheel_delta,
                                               uint8_t output_mouse_joy)
{
    portENTER_CRITICAL();
    base_joy = output_mouse_joy;
    if (wheel_delta != 0) {
        uint8_t next_head = queue_head + 1;
        if (next_head >= queue_size) {
            next_head = 0;
        }
        if (next_head == queue_tail) {
            uint8_t last = (queue_head == 0) ? (queue_size - 1) : (queue_head - 1);
            pending_queue[last] += wheel_delta;
        } else {
            pending_queue[queue_head] = wheel_delta;
            queue_head = next_head;
        }
    }
    portEXIT_CRITICAL();
}

static bool usb_hid_take_native_wheel_input(int *pending_queue,
                                            uint8_t queue_size,
                                            uint8_t& queue_head,
                                            uint8_t& queue_tail,
                                            uint8_t& base_joy,
                                            int& wheel_delta,
                                            uint8_t& output_mouse_joy)
{
    bool have_delta = false;
    portENTER_CRITICAL();
    output_mouse_joy = base_joy;
    if (queue_tail != queue_head) {
        wheel_delta = pending_queue[queue_tail];
        queue_tail++;
        if (queue_tail >= queue_size) {
            queue_tail = 0;
        }
        have_delta = true;
    } else {
        wheel_delta = 0;
    }
    portEXIT_CRITICAL();
    return have_delta;
}

static void usb_hid_clear_native_wheel_input(uint8_t& queue_head,
                                             uint8_t& queue_tail,
                                             uint8_t& base_joy,
                                             uint8_t output_mouse_joy)
{
    portENTER_CRITICAL();
    queue_head = 0;
    queue_tail = 0;
    base_joy = output_mouse_joy;
    portEXIT_CRITICAL();
}

static void usb_hid_set_native_wheel_output_active(uint8_t& output_active, bool active)
{
    portENTER_CRITICAL();
    output_active = active ? 1 : 0;
    portEXIT_CRITICAL();
}

static bool usb_hid_get_native_wheel_output_active(uint8_t& output_active)
{
    bool active;
    portENTER_CRITICAL();
    active = output_active != 0;
    portEXIT_CRITICAL();
    return active;
}

static void usb_hid_find_sibling_active_report_functions(UsbDevice *device, UsbInterface *skip,
                                                         bool& report_keyboard, bool& report_mouse)
{
    report_keyboard = false;
    report_mouse = false;
    if (!device) {
        return;
    }

    for (int i = 0; i < device->num_interfaces; i++) {
        UsbInterface *candidate = device->interfaces[i];
        if (!candidate || candidate == skip) {
            continue;
        }
        UsbDriver *driver = candidate->getDriver();
        if (!driver) {
            continue;
        }

        UsbHidDriver *hid_driver = static_cast<UsbHidDriver *>(driver);
        if (!report_mouse && hid_driver->has_active_report_mouse()) {
            report_mouse = true;
        }
        if (!report_keyboard && hid_driver->has_active_report_keyboard()) {
            report_keyboard = true;
        }

        if (report_keyboard && report_mouse) {
            return;
        }
    }
}

static void usb_hid_relinquish_sibling_boot_functions(UsbDevice *device, UsbInterface *current,
                                                      bool take_over_keyboard, bool take_over_mouse)
{
    if (!device || !(take_over_keyboard || take_over_mouse)) {
        return;
    }

    for (int i = 0; i < device->num_interfaces; i++) {
        UsbInterface *candidate = device->interfaces[i];
        if (!candidate || candidate == current) {
            continue;
        }

        UsbDriver *driver = candidate->getDriver();
        if (!driver) {
            continue;
        }

        UsbHidDriver *hid_driver = static_cast<UsbHidDriver *>(driver);
        hid_driver->relinquish_boot_function(take_over_keyboard, take_over_mouse);
    }
}

static void usb_hid_queue_key(int key, int repeat)
{
    if (repeat <= 0) {
        return;
    }
    if (repeat > (USB_KEY_BUFFER_SIZE - 1)) {
        repeat = USB_KEY_BUFFER_SIZE - 1;
    }
    system_usb_keyboard.push_head_repeat(key, repeat);
}

static void usb_hid_queue_menu_key(int key, int repeat, int max_pending)
{
    if (repeat <= 0) {
        return;
    }

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
    int available = max_pending - pending;
    if (available <= 0) {
        return;
    }
    if (repeat > available) {
        repeat = available;
    }

    system_usb_keyboard.push_head_repeat(key, repeat);
}

static void usb_hid_queue_wheel_keys(int wheel_h, int wheel_v)
{
    if (wheel_v > 0) {
        usb_hid_queue_key(KEY_UP, wheel_v);
    } else if (wheel_v < 0) {
        usb_hid_queue_key(KEY_DOWN, -wheel_v);
    }

    if (wheel_h > 0) {
        usb_hid_queue_key(KEY_RIGHT, wheel_h);
    } else if (wheel_h < 0) {
        usb_hid_queue_key(KEY_LEFT, -wheel_h);
    }
}

static void usb_hid_queue_motion_keys(int motion_x, int motion_y)
{
    motion_y = HidMouseInterpreter::normalizeCursorVerticalMotion(motion_y);
    int horizontal = HidMouseInterpreter::scaleCursorMotionKeys(motion_x);
    int vertical = HidMouseInterpreter::scaleCursorMotionKeys(motion_y);

    if (motion_y > 0) {
        usb_hid_queue_key(KEY_UP, vertical);
    } else if (motion_y < 0) {
        usb_hid_queue_key(KEY_DOWN, vertical);
    }

    if (motion_x > 0) {
        usb_hid_queue_key(KEY_RIGHT, horizontal);
    } else if (motion_x < 0) {
        usb_hid_queue_key(KEY_LEFT, horizontal);
    }
}

// Entry point for call-backs.
void UsbHidDriver_interrupt_callback(void *object) {
	((UsbHidDriver *)object)->interrupt_handler();
}

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary system objects
/*********************************************************************/
// tester instance
FactoryRegistrator<UsbInterface *, UsbDriver *> hid_tester(UsbInterface :: getUsbDriverFactory(), UsbHidDriver :: test_driver);

UsbHidDriver :: UsbHidDriver(UsbInterface *intf) : UsbDriver(intf)
{
    device = NULL;
    host = NULL;
    interface = intf;
    irq_in = 0;
    irq_transaction = 0;
    keyboard = false;
    mouse = false;
    descriptor_keyboard = false;
    descriptor_mouse = false;
    mouse_x = mouse_y = 0;
    mouse_joy = 0x1F;
    memset(native_wheel_delta_queue, 0, sizeof(native_wheel_delta_queue));
    native_wheel_queue_head = 0;
    native_wheel_queue_tail = 0;
    native_wheel_base_joy = 0x1F;
    native_wheel_output_active = 0;
    memset(keyboard_data, 0, sizeof(keyboard_data));
    memset(&rep_button1, 0, sizeof(rep_button1));
    memset(&rep_button2, 0, sizeof(rep_button2));
    memset(&rep_button3, 0, sizeof(rep_button3));
    memset(&rep_mouse_x, 0, sizeof(rep_mouse_x));
    memset(&rep_mouse_y, 0, sizeof(rep_mouse_y));
    memset(&rep_wheel_v, 0, sizeof(rep_wheel_v));
    memset(&rep_wheel_h, 0, sizeof(rep_wheel_h));
    has_button1 = false;
    has_button2 = false;
    has_button3 = false;
    has_wheel_v = false;
    has_wheel_h = false;
    previous_left_button_pressed = false;
    mouse_registered = false;
    menu_left_button_consumed = false;
    menu_right_button_consumed = false;
    menu_wheel_h_latch = 0;
    menu_wheel_v_latch = 0;
    menu_wheel_v_mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
    menu_wheel_v_burst_accumulator = 0;
    menu_wheel_v_burst_direction = 0;
    menu_wheel_v_last_tick = 0;
    wheel_axis_h_remainder = 0;
    wheel_axis_v_remainder = 0;
    wheel_key_h_remainder = 0;
    wheel_key_v_remainder = 0;
    wheel_step_accumulator = 0;
    wheel_pulse_phase = HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE;
    wheel_pulse_mask = 0;
    wheel_pulse_next_tick = 0;
    wheel_pulse_burst_direction = 0;
    wheel_pulse_burst_count = 0;
    pointer_sensitivity_setting = -1;
    pointer_sensitivity_remainder_x = 0;
    pointer_sensitivity_remainder_y = 0;
    adaptive_accel_ema_x16 = 0;
    adaptive_accel_scale_factor = HidMouseInterpreter::ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
}

UsbHidDriver :: ~UsbHidDriver()
{
}

bool UsbHidDriver :: has_active_report_keyboard(void) const
{
    return keyboard && descriptor_keyboard;
}

bool UsbHidDriver :: has_active_report_mouse(void) const
{
    return mouse && descriptor_mouse;
}

void UsbHidDriver :: relinquish_boot_function(bool release_keyboard, bool release_mouse)
{
    bool should_release_keyboard = release_keyboard && keyboard && !descriptor_keyboard;
    bool should_release_mouse = release_mouse && mouse && !descriptor_mouse;
    if (!(should_release_keyboard || should_release_mouse)) {
        return;
    }

    disable();
    if (should_release_mouse) {
        usb_hid_clear_visibility_if_source_matches(usb_hid_mouse_visibility, device, interface);
    }
    if (should_release_keyboard) {
        usb_hid_remove_keyboard_source(device, interface);
        usb_hid_clear_visibility_if_source_matches(usb_hid_keyboard_visibility, device, interface);
    }
    keyboard = false;
    mouse = false;
}

UsbDriver * UsbHidDriver :: test_driver(UsbInterface *intf)
{
	if (intf->getInterfaceDescriptor()->interface_class != 3) {
		return NULL;
	}
	printf("** USB HID Device found!\n");
	return new UsbHidDriver(intf);
}

void UsbHidDriver :: install(UsbInterface *intf)
{
	UsbDevice *dev = intf->getParentDevice();
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    interface = intf;
    device = intf->getParentDevice();

    dev->set_configuration(dev->get_device_config()->config_value);

	dev->set_interface(interface->getInterfaceDescriptor()->interface_number,
			interface->getInterfaceDescriptor()->alternate_setting);

	int len = 0;
	uint8_t *reportDescriptor = intf->getHidReportDescriptor(&len);

    uint8_t c_set_idle[]     = { 0x21, 0x0A, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 };
    uint8_t c_set_protocol[] = { 0x21, 0x0B, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 };
    c_set_idle[4] = interface->getInterfaceDescriptor()->interface_number;
    c_set_protocol[4] = interface->getInterfaceDescriptor()->interface_number;

    if (reportDescriptor && (len > 0)) {
        HidReportParser parser;
        if (parser.decode(&report_items, reportDescriptor, len) == 0) {
            t_hid_mouse_fields mouse_fields;
            if (report_items.locateMouseFields(mouse_fields)) {
                descriptor_mouse = true;
                rep_mouse_x = mouse_fields.mouse_x;
                rep_mouse_y = mouse_fields.mouse_y;
                rep_button1 = mouse_fields.button1;
                rep_button2 = mouse_fields.button2;
                rep_button3 = mouse_fields.button3;
                rep_wheel_v = mouse_fields.wheel_v;
                rep_wheel_h = mouse_fields.wheel_h;
                has_button1 = mouse_fields.has_button1;
                has_button2 = mouse_fields.has_button2;
                has_button3 = mouse_fields.has_button3;
                has_wheel_v = mouse_fields.has_wheel_v;
                has_wheel_h = mouse_fields.has_wheel_h;
            }

            t_hid_keyboard_fields keyboard_fields;
            if (report_items.locateKeyboardFields(keyboard_fields)) {
                descriptor_keyboard = true;
            }
        }
    }

    t_usb_hid_interface_capabilities current_capabilities = {
        descriptor_keyboard,
        descriptor_mouse,
        (interface->getInterfaceDescriptor()->sub_class == 1) && (interface->getInterfaceDescriptor()->protocol == 1),
        (interface->getInterfaceDescriptor()->sub_class == 1) && (interface->getInterfaceDescriptor()->protocol == 2)
    };
    bool sibling_active_report_keyboard = false;
    bool sibling_active_report_mouse = false;
    usb_hid_find_sibling_active_report_functions(dev, interface,
                                                 sibling_active_report_keyboard,
                                                 sibling_active_report_mouse);

    t_usb_hid_interface_selection selection = usb_hid_select_interface(current_capabilities,
                                                                       sibling_active_report_keyboard,
                                                                       sibling_active_report_mouse);
    keyboard = selection.keyboard;
    mouse = selection.mouse;

    if (!selection.use_report_protocol && current_capabilities.boot_keyboard && keyboard) {
        printf("Boot Keyboard found!\n");
    }
    if (!selection.use_report_protocol && current_capabilities.boot_mouse && mouse) {
        printf("Boot Mouse found!\n");
    }

    if (!(keyboard || mouse)) {
        return;
    }

	struct t_endpoint_descriptor *iin = interface->find_endpoint(0x83);
    if (!iin) {
        printf("No interrupt IN endpoint found for HID interface %d.\n", interface->getInterfaceDescriptor()->interface_number);
        keyboard = false;
        mouse = false;
        descriptor_keyboard = false;
        descriptor_mouse = false;
        return;
    }

    host->initialize_pipe(&ipipe, device, iin);
	ipipe.Interval = 160; // 50 Hz
    uint16_t max_packet = iin->max_packet_size;
    if ((max_packet == 0) || (max_packet > sizeof(irq_data))) {
        max_packet = sizeof(irq_data);
    }
    ipipe.Length = max_packet;
    ipipe.MaxTrans = max_packet;
	ipipe.buffer = this->irq_data;

	irq_transaction = host->allocate_input_pipe(&ipipe, UsbHidDriver_interrupt_callback, this);
    if (irq_transaction <= 0) {
        printf("Failed to allocate input pipe for HID interface %d.\n", interface->getInterfaceDescriptor()->interface_number);
        keyboard = false;
        mouse = false;
        descriptor_keyboard = false;
        descriptor_mouse = false;
        return;
    }

    if (interface->getInterfaceDescriptor()->sub_class == 1) {
        c_set_protocol[2] = selection.use_report_protocol ? 1 : 0;
        host->control_exchange(&dev->control_pipe, c_set_protocol, 8, NULL, 0);
    }
    host->control_exchange(&dev->control_pipe, c_set_idle, 8, NULL, 0);
    if (mouse && !mouse_registered) {
        usb_hid_active_mouse_interfaces++;
        mouse_registered = true;
        usb_hid_apply_mouse_output_enable();
    }
    int interface_number = usb_hid_get_source_interface(interface);
    if (mouse && usb_hid_should_claim_visibility(usb_hid_mouse_visibility.source_device,
                                                 usb_hid_mouse_visibility.source_interface,
                                                 dev, interface_number, false)) {
        usb_hid_set_visibility(usb_hid_mouse_visibility, dev, interface, descriptor_mouse);
    }
    if (keyboard && usb_hid_should_claim_visibility(usb_hid_keyboard_visibility.source_device,
                                                    usb_hid_keyboard_visibility.source_interface,
                                                    dev, interface_number, false)) {
        usb_hid_set_visibility(usb_hid_keyboard_visibility, dev, interface, descriptor_keyboard);
    }
    if (mouse || keyboard) {
        usb_hid_publish_visibility();
    }
    if (selection.use_report_protocol) {
        usb_hid_relinquish_sibling_boot_functions(dev, interface, descriptor_keyboard, descriptor_mouse);
    }
    host->resume_input_pipe(irq_transaction);
}

void UsbHidDriver :: disable()
{
    if ((host) && (irq_transaction > 0)) {
	host->free_input_pipe(irq_transaction);
        irq_transaction = 0;
    }
    if (mouse_registered) {
        if (usb_hid_active_mouse_interfaces > 0) {
            usb_hid_active_mouse_interfaces--;
        }
        mouse_registered = false;
        usb_hid_apply_mouse_output_enable();
    }

    mouse_joy = 0x1F;
    usb_hid_clear_native_wheel_input(native_wheel_queue_head,
                                     native_wheel_queue_tail,
                                     native_wheel_base_joy,
                                     0x1F);
    usb_hid_set_native_wheel_output_active(native_wheel_output_active, false);
#if U64
    if (usb_hid_active_mouse_interfaces == 0) {
        C64_JOY1_SWOUT = 0x1F;
    }
#endif
    previous_left_button_pressed = false;
    menu_left_button_consumed = false;
    menu_right_button_consumed = false;
    menu_wheel_h_latch = 0;
    menu_wheel_v_latch = 0;
    menu_wheel_v_mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
    menu_wheel_v_burst_accumulator = 0;
    menu_wheel_v_burst_direction = 0;
    menu_wheel_v_last_tick = 0;
    wheel_axis_h_remainder = 0;
    wheel_axis_v_remainder = 0;
    wheel_key_h_remainder = 0;
    wheel_key_v_remainder = 0;
    wheel_step_accumulator = 0;
    wheel_pulse_phase = HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE;
    wheel_pulse_mask = 0;
    wheel_pulse_next_tick = 0;
    wheel_pulse_burst_direction = 0;
    wheel_pulse_burst_count = 0;
    pointer_sensitivity_setting = -1;
    pointer_sensitivity_remainder_x = 0;
    pointer_sensitivity_remainder_y = 0;
    adaptive_accel_ema_x16 = 0;
    adaptive_accel_scale_factor = HidMouseInterpreter::ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
}

void UsbHidDriver :: deinstall(UsbInterface *intf)
{
    disable();
    if (mouse) {
        usb_hid_clear_visibility_if_source_matches(usb_hid_mouse_visibility, device, interface);
    }
    if (keyboard) {
        usb_hid_remove_keyboard_source(device, interface);
        usb_hid_clear_visibility_if_source_matches(usb_hid_keyboard_visibility, device, interface);
    }
    usb_hid_publish_visibility();
    printf("HID deinstalled.\n");
}

void UsbHidDriver :: poll(void)
{
#if U64
    if (!mouse) {
        return;
    }

    uint8_t output_mouse_joy = 0x1F;
    int native_wheel_delta = 0;
    bool had_native_wheel_input = false;

    while (usb_hid_take_native_wheel_input(native_wheel_delta_queue,
                                           sizeof(native_wheel_delta_queue) / sizeof(native_wheel_delta_queue[0]),
                                           native_wheel_queue_head,
                                           native_wheel_queue_tail,
                                           native_wheel_base_joy,
                                           native_wheel_delta,
                                           output_mouse_joy)) {
        had_native_wheel_input = true;
        HidMouseInterpreter::mergeNativeWheelBurst(native_wheel_delta,
                                                   USB_HID_MOUSE_WHEEL_BURST_LIMIT,
                                                   wheel_pulse_burst_direction,
                                                   wheel_pulse_burst_count);
    }

    output_mouse_joy = HidMouseInterpreter::applyWheelPulseMask(output_mouse_joy,
                                                                HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE,
                                                                0);

    if (!HidMouseInterpreter::mouseModeRoutesWheelToNative(usb_hid_get_mouse_mode())) {
        if ((wheel_pulse_phase != HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE) ||
            (wheel_pulse_burst_count != 0) ||
            (wheel_step_accumulator != 0) ||
            had_native_wheel_input) {
            usb_hid_reset_native_wheel_state(wheel_step_accumulator,
                                             wheel_pulse_phase,
                                             wheel_pulse_mask,
                                             wheel_pulse_next_tick,
                                             wheel_pulse_burst_direction,
                                             wheel_pulse_burst_count);
            C64_JOY1_SWOUT = output_mouse_joy;
        }
        usb_hid_set_native_wheel_output_active(native_wheel_output_active, false);
        return;
    }

    if ((wheel_pulse_phase != HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE) ||
        (wheel_pulse_burst_count != 0)) {
        output_mouse_joy = usb_hid_service_native_wheel(output_mouse_joy,
                                                        wheel_pulse_burst_direction,
                                                        wheel_pulse_burst_count,
                                                        wheel_pulse_phase,
                                                        wheel_pulse_mask,
                                                        wheel_pulse_next_tick,
                                                        (uint32_t)xTaskGetTickCount());
    }

    usb_hid_set_native_wheel_output_active(native_wheel_output_active,
                                           (wheel_pulse_phase != HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE) ||
                                           (wheel_pulse_burst_count != 0));
    C64_JOY1_SWOUT = output_mouse_joy;
#endif
}

void UsbHidDriver :: interrupt_handler()
{
    int data_len = host->getReceivedLength(irq_transaction);
    if (data_len <= 0) {
        host->resume_input_pipe(this->irq_transaction);
        return;
    }
    if (data_len < int(sizeof(irq_data))) {
        memset(irq_data + data_len, 0, sizeof(irq_data) - data_len);
    }

    // printf("I%d: ", interface->getNumber());
    // for(int i=0;i<data_len;i++) {
    //     printf("%b ", irq_data[i]);
    // } printf("\n");

    bool handled = false;

    if (keyboard) {
        bool have_keyboard_report = false;
        if (descriptor_keyboard) {
            have_keyboard_report = report_items.synthesizeBootKeyboardReport(irq_data, data_len, keyboard_data);
        } else {
            HidBootProtocol::copyKeyboardReport(irq_data, data_len, keyboard_data);
            have_keyboard_report = true;
        }
        if (have_keyboard_report) {
            usb_hid_update_keyboard_source(device, interface, keyboard_data);
            if (usb_hid_keyboard_report_has_activity(keyboard_data)) {
                usb_hid_set_visibility(usb_hid_keyboard_visibility, device, interface, descriptor_keyboard);
                usb_hid_publish_visibility();
            }
            handled = true;
        } else if (descriptor_keyboard && report_items.hasReportID && report_items.hasInputReportId(irq_data[0])) {
            handled = true;
        }
    }

    if (mouse) {
        int mouse_mode = usb_hid_get_mouse_mode();
        bool motion_to_cursor = HidMouseInterpreter::mouseModeRoutesMotionToCursor(mouse_mode);
        bool motion_to_pointer = HidMouseInterpreter::mouseModeRoutesMotionToPointer(mouse_mode);
        bool wheel_to_cursor = HidMouseInterpreter::mouseModeRoutesWheelToCursor(mouse_mode);
        bool wheel_to_pointer = HidMouseInterpreter::mouseModeRoutesWheelToPointer(mouse_mode);
        bool menu_override = usb_hid_menu_override_active();
        int wheel_v = 0;
        int wheel_h = 0;
        int motion_x = 0;
        int motion_y = 0;
        bool handled_mouse = false;
        bool left_button_pressed = previous_left_button_pressed;
        bool right_button_pressed = (mouse_joy & 0x01) == 0;
        uint8_t previous_mouse_joy = mouse_joy;

        if (descriptor_mouse) {
            bool axis_report = HidReport::hasValue(irq_data, data_len, rep_mouse_x) || HidReport::hasValue(irq_data, data_len, rep_mouse_y);
            bool button1_report = has_button1 && HidReport::hasValue(irq_data, data_len, rep_button1);
            bool button2_report = has_button2 && HidReport::hasValue(irq_data, data_len, rep_button2);
            bool button3_report = has_button3 && HidReport::hasValue(irq_data, data_len, rep_button3);
            bool wheel_v_report = has_wheel_v && HidReport::hasValue(irq_data, data_len, rep_wheel_v);
            bool wheel_h_report = has_wheel_h && HidReport::hasValue(irq_data, data_len, rep_wheel_h);

            handled_mouse = axis_report || button1_report || button2_report || button3_report || wheel_v_report || wheel_h_report;

            if (button1_report) {
                usb_hid_set_mouse_button(mouse_joy, 0x10, HidReport::getValueFromData(irq_data, data_len, rep_button1) != 0);
            }
            if (button2_report) {
                usb_hid_set_mouse_button(mouse_joy, 0x01, HidReport::getValueFromData(irq_data, data_len, rep_button2) != 0);
            }
            if (button3_report) {
                usb_hid_set_mouse_button(mouse_joy, 0x02, HidReport::getValueFromData(irq_data, data_len, rep_button3) != 0);
            }
            if (axis_report) {
                motion_x = HidReport::getValueFromData(irq_data, data_len, rep_mouse_x);
                motion_y = HidReport::getValueFromData(irq_data, data_len, rep_mouse_y);
                int raw_motion_x = motion_x;
                int raw_motion_y = motion_y;
                int sensitivity = usb_hid_get_mouse_sensitivity();
                int acceleration = usb_hid_get_mouse_acceleration();
                usb_hid_apply_pointer_sensitivity(motion_x, motion_y, sensitivity,
                                                  pointer_sensitivity_setting,
                                                  pointer_sensitivity_remainder_x,
                                                  pointer_sensitivity_remainder_y);
                usb_hid_apply_pointer_acceleration(motion_x, motion_y,
                                                   raw_motion_x, raw_motion_y, acceleration,
                                                   adaptive_accel_ema_x16, adaptive_accel_scale_factor);
                if (motion_to_pointer || menu_override) {
                    HidMouseInterpreter::applyRelativeMotion(mouse_x, mouse_y, motion_x, motion_y);
                }
            }
            if (wheel_v_report) {
                wheel_v = HidReport::getValueFromData(irq_data, data_len, rep_wheel_v);
            }
            if (wheel_h_report) {
                wheel_h = HidReport::getValueFromData(irq_data, data_len, rep_wheel_h);
            }
            left_button_pressed = (mouse_joy & 0x10) == 0;
            right_button_pressed = (mouse_joy & 0x01) == 0;
        } else {
            t_hid_boot_mouse_sample sample;
            HidBootProtocol::decodeMouseReport(irq_data, data_len, sample);
            handled_mouse = true;
            mouse_joy = 0x1F;
            usb_hid_set_mouse_button(mouse_joy, 0x10, (sample.buttons & 0x01) != 0);
            usb_hid_set_mouse_button(mouse_joy, 0x01, (sample.buttons & 0x02) != 0);
            usb_hid_set_mouse_button(mouse_joy, 0x02, (sample.buttons & 0x04) != 0);
            motion_x = sample.x;
            motion_y = sample.y;
            int raw_motion_x = motion_x;
            int raw_motion_y = motion_y;
            int sensitivity = usb_hid_get_mouse_sensitivity();
            int acceleration = usb_hid_get_mouse_acceleration();
            usb_hid_apply_pointer_sensitivity(motion_x, motion_y, sensitivity,
                                              pointer_sensitivity_setting,
                                              pointer_sensitivity_remainder_x,
                                              pointer_sensitivity_remainder_y);
            usb_hid_apply_pointer_acceleration(motion_x, motion_y,
                                               raw_motion_x, raw_motion_y, acceleration,
                                               adaptive_accel_ema_x16, adaptive_accel_scale_factor);
            if (motion_to_pointer || menu_override) {
                HidMouseInterpreter::applyRelativeMotion(mouse_x, mouse_y, motion_x, motion_y);
            }
            left_button_pressed = (sample.buttons & 0x01) != 0;
            right_button_pressed = (sample.buttons & 0x02) != 0;
        }

        if (handled_mouse) {
            bool reversed_wheel = usb_hid_get_wheel_direction() == WHEEL_DIRECTION_REVERSED;
            int scroll_factor = usb_hid_get_scroll_factor();
            uint8_t output_mouse_joy = mouse_joy;
            int wheel_h_normalized = HidMouseInterpreter::normalizeHorizontalWheel(wheel_h);
            int wheel_v_normalized = HidMouseInterpreter::normalizeVerticalWheel(wheel_v);
            int wheel_h_keys = HidMouseInterpreter::applyWheelDirection(wheel_h_normalized, reversed_wheel);
            int wheel_v_keys = HidMouseInterpreter::applyWheelDirection(wheel_v_normalized, reversed_wheel);
            int wheel_h_axis = wheel_h_keys;
            int wheel_v_axis = -wheel_v_keys;

            if (usb_hid_mouse_report_has_activity(motion_x, motion_y, wheel_h_normalized, wheel_v_normalized, mouse_joy, previous_mouse_joy)) {
                usb_hid_set_visibility(usb_hid_mouse_visibility, device, interface, descriptor_mouse);
                usb_hid_publish_visibility();
            }

            if (menu_override) {
                output_mouse_joy |= 0x11;
                if (left_button_pressed) {
                    if (!menu_left_button_consumed) {
                        usb_hid_queue_menu_key(KEY_RETURN, 1, 1);
                        menu_left_button_consumed = true;
                    }
                } else {
                    menu_left_button_consumed = false;
                }
                if (right_button_pressed) {
                    if (!menu_right_button_consumed) {
                        usb_hid_queue_menu_key(KEY_BREAK, 1, 1);
                        menu_right_button_consumed = true;
                    }
                } else {
                    menu_right_button_consumed = false;
                }
            } else {
                menu_left_button_consumed = left_button_pressed;
                menu_right_button_consumed = right_button_pressed;
            }

            if (!menu_override && motion_to_cursor) {
                usb_hid_queue_motion_keys(motion_x, motion_y);
            }

            if ((wheel_v_normalized != 0) || (wheel_h_normalized != 0)) {
                if (menu_override) {
                    wheel_axis_h_remainder = 0;
                    wheel_axis_v_remainder = 0;
                    wheel_key_h_remainder = 0;
                    wheel_key_v_remainder = 0;
                    uint32_t now_ticks = (uint32_t)xTaskGetTickCount();
                    int menu_wheel_h = usb_hid_filter_menu_wheel_step(HidMouseInterpreter::scaleMenuWheelKeys(wheel_h_keys), menu_wheel_h_latch);
                    int menu_wheel_v = HidMouseInterpreter::scaleMenuWheelBurst(
                        wheel_v_keys,
                        now_ticks,
                        menu_wheel_v_last_tick,
                        menu_wheel_v_mode,
                        menu_wheel_v_burst_direction,
                        menu_wheel_v_burst_accumulator,
                        USB_HID_MENU_WHEEL_FAST_GAP_TICKS,
                        USB_HID_MENU_WHEEL_SLOW_GAP_TICKS,
                        USB_HID_MENU_WHEEL_RESET_GAP_TICKS,
                        USB_HID_MENU_WHEEL_EXTRA_STEP_THRESHOLD);
                    menu_wheel_v_latch = 0;
                    if (menu_wheel_v > 0) {
                        usb_hid_queue_menu_key(KEY_UP, menu_wheel_v, USB_HID_MENU_MAX_PENDING_VERTICAL_KEYS);
                    } else if (menu_wheel_v < 0) {
                        usb_hid_queue_menu_key(KEY_DOWN, -menu_wheel_v, USB_HID_MENU_MAX_PENDING_VERTICAL_KEYS);
                    }
                    usb_hid_queue_wheel_keys(menu_wheel_h, 0);
                } else if (wheel_to_cursor) {
                    menu_wheel_h_latch = 0;
                    menu_wheel_v_latch = 0;
                    menu_wheel_v_mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
                    menu_wheel_v_burst_accumulator = 0;
                    menu_wheel_v_burst_direction = 0;
                    menu_wheel_v_last_tick = 0;
                    wheel_axis_h_remainder = 0;
                    wheel_axis_v_remainder = 0;
                    wheel_step_accumulator = 0;
                    usb_hid_queue_wheel_keys(
                        HidMouseInterpreter::scaleHorizontalWheelKeys(wheel_h_keys, scroll_factor, wheel_key_h_remainder),
                        HidMouseInterpreter::scaleVerticalWheelKeys(wheel_v_keys, scroll_factor, wheel_key_v_remainder));
                } else if (wheel_to_pointer) {
                    menu_wheel_h_latch = 0;
                    menu_wheel_v_latch = 0;
                    menu_wheel_v_mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
                    menu_wheel_v_burst_accumulator = 0;
                    menu_wheel_v_burst_direction = 0;
                    menu_wheel_v_last_tick = 0;
                    wheel_key_h_remainder = 0;
                    wheel_key_v_remainder = 0;
                    wheel_step_accumulator = 0;
                    HidMouseInterpreter::applyWheelAxisDeltas(
                        mouse_x,
                        mouse_y,
                        HidMouseInterpreter::scaleHorizontalWheelAxisDelta(wheel_h_axis, scroll_factor, wheel_axis_h_remainder),
                        HidMouseInterpreter::scaleVerticalWheelAxisDelta(wheel_v_axis, scroll_factor, wheel_axis_v_remainder));
                } else if (HidMouseInterpreter::mouseModeRoutesWheelToNative(mouse_mode)) {
                    menu_wheel_h_latch = 0;
                    menu_wheel_v_latch = 0;
                    menu_wheel_v_mode = HidMouseInterpreter::MENU_WHEEL_MODE_PRECISE;
                    menu_wheel_v_burst_accumulator = 0;
                    menu_wheel_v_burst_direction = 0;
                    menu_wheel_v_last_tick = 0;
                    wheel_axis_h_remainder = 0;
                    wheel_axis_v_remainder = 0;
                    wheel_key_h_remainder = 0;
                    wheel_key_v_remainder = 0;
                    int wheel_steps = HidMouseInterpreter::applyWheelDirection(
                        HidMouseInterpreter::accumulateNativeWheelSteps(
                            wheel_v_normalized,
                            scroll_factor,
                            wheel_step_accumulator),
                        reversed_wheel);
                    usb_hid_publish_native_wheel_input(native_wheel_delta_queue,
                                                       sizeof(native_wheel_delta_queue) / sizeof(native_wheel_delta_queue[0]),
                                                       native_wheel_queue_head,
                                                       native_wheel_queue_tail,
                                                       native_wheel_base_joy,
                                                       wheel_steps,
                                                       output_mouse_joy);
                }
            } else {
                menu_wheel_h_latch = 0;
                menu_wheel_v_latch = 0;
            }

            if (HidMouseInterpreter::mouseModeRoutesWheelToNative(mouse_mode)) {
                output_mouse_joy = HidMouseInterpreter::applyWheelPulseMask(output_mouse_joy,
                                                                            HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE,
                                                                            0);
                usb_hid_publish_native_wheel_input(native_wheel_delta_queue,
                                                   sizeof(native_wheel_delta_queue) / sizeof(native_wheel_delta_queue[0]),
                                                   native_wheel_queue_head,
                                                   native_wheel_queue_tail,
                                                   native_wheel_base_joy,
                                                   0,
                                                   output_mouse_joy);
                if (!usb_hid_get_native_wheel_output_active(native_wheel_output_active)) {
                    C64_JOY1_SWOUT = output_mouse_joy;
                }
            } else {
                output_mouse_joy = HidMouseInterpreter::applyWheelPulseMask(output_mouse_joy,
                                                                            HidMouseInterpreter::WHEEL_PULSE_PHASE_IDLE,
                                                                            0);
                usb_hid_clear_native_wheel_input(native_wheel_queue_head,
                                                 native_wheel_queue_tail,
                                                 native_wheel_base_joy,
                                                 output_mouse_joy);
            }

#if U64
            if (!HidMouseInterpreter::mouseModeRoutesWheelToNative(mouse_mode)) {
                C64_JOY1_SWOUT = output_mouse_joy;
            }
            C64_PADDLE_1_X = mouse_x & 0x7F;
            C64_PADDLE_1_Y = mouse_y & 0x7F;
#else
            printf("Mouse: %4x,%4x %b\n", mouse_x, mouse_y, output_mouse_joy);
#endif
            previous_left_button_pressed = left_button_pressed;
            handled = true;
        }
    }

    if (!handled) {
	    printf("HID (ADDR=%d) IRQ data: ", device->current_address);
	    for(int i=0;i<data_len;i++) {
	        printf("%b ", irq_data[i]);
	    } printf("\n");
	}
    host->resume_input_pipe(this->irq_transaction);
}

void UsbHidDriver :: pipe_error(int pipe) // called from IRQ!
{
	printf("UsbHid ERROR on IRQ pipe.\n");
}

