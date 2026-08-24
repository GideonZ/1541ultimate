#ifndef USB_HID_SELECTION_H
#define USB_HID_SELECTION_H

#include <stdint.h>

class UsbDevice;

struct t_usb_hid_interface_capabilities
{
    bool descriptor_keyboard;
    bool descriptor_mouse;
    bool boot_keyboard;
    bool boot_mouse;
};

struct t_usb_hid_interface_selection
{
    bool keyboard;
    bool mouse;
    bool use_report_protocol;
};

static inline bool usb_hid_source_matches(const UsbDevice *visible_device,
                                          int visible_interface,
                                          const UsbDevice *candidate_device,
                                          int candidate_interface)
{
    return (visible_device == candidate_device) && (visible_interface == candidate_interface);
}

static inline bool usb_hid_should_claim_visibility(const UsbDevice *visible_device,
                                                   int visible_interface,
                                                   const UsbDevice *candidate_device,
                                                   int candidate_interface,
                                                   bool has_activity)
{
    if (has_activity) {
        return true;
    }
    if (!visible_device) {
        return true;
    }
    return usb_hid_source_matches(visible_device, visible_interface,
                                  candidate_device, candidate_interface);
}

static inline t_usb_hid_interface_selection usb_hid_select_interface(const t_usb_hid_interface_capabilities& current,
                                                                     bool sibling_active_report_keyboard,
                                                                     bool sibling_active_report_mouse)
{
    t_usb_hid_interface_selection selection = { false, false, false };

    if (current.descriptor_keyboard) {
        selection.keyboard = true;
        selection.use_report_protocol = true;
    }
    if (current.descriptor_mouse) {
        selection.mouse = true;
        selection.use_report_protocol = true;
    }
    if (current.boot_keyboard && !current.descriptor_keyboard && !sibling_active_report_keyboard) {
        selection.keyboard = true;
    }
    if (current.boot_mouse && !current.descriptor_mouse && !sibling_active_report_mouse) {
        selection.mouse = true;
    }
    return selection;
}

// SET_IDLE durations are in units of 4ms. 25 re-sends a held key every 100ms,
// well inside the 320ms the menu takes to start repeating. A shorter period
// would add more traffic to the USB event task this fix already waits on.
static const uint8_t USB_HID_SET_IDLE_UNITS = 25;
static const int USB_HID_SET_IDLE_PERIOD_MS = 4 * USB_HID_SET_IDLE_UNITS;

// SET_IDLE carries report id 0, which covers every input report of the interface,
// and the HID specification wants an infinite idle period for mice. Only a
// keyboard-only interface is asked for a rate; anything with a mouse keeps 0.
static inline uint8_t usb_hid_set_idle_units(bool keyboard, bool mouse)
{
    return (keyboard && !mouse) ? USB_HID_SET_IDLE_UNITS : 0;
}

// control_exchange() returns 0 for a request without a data stage whether or not
// its status phase completed, and a keyboard may acknowledge SET_IDLE without
// storing it. Only a GET_IDLE that reads the duration back proves it was stored.
static inline bool usb_hid_idle_rate_accepted(uint8_t requested_units,
                                              int get_idle_result,
                                              uint8_t reported_units)
{
    return (requested_units != 0) && (get_idle_result == 1) && (reported_units == requested_units);
}

// What to do when a sibling interface takes over a boot function. Stopping the
// interface stops every function it served, so a composite interface must drop its
// merged keyboard source even when only its mouse was taken over; otherwise its
// last report stays merged and a key that was down stays pressed.
struct t_usb_hid_relinquish_actions
{
    bool relinquish;
    bool release_keyboard;
    bool release_mouse;
};

static inline t_usb_hid_relinquish_actions usb_hid_relinquish_actions(bool keyboard, bool mouse,
                                                                     bool descriptor_keyboard,
                                                                     bool descriptor_mouse,
                                                                     bool take_over_keyboard,
                                                                     bool take_over_mouse)
{
    t_usb_hid_relinquish_actions actions = { false, false, false };
    bool taken_keyboard = take_over_keyboard && keyboard && !descriptor_keyboard;
    bool taken_mouse = take_over_mouse && mouse && !descriptor_mouse;

    if (!(taken_keyboard || taken_mouse)) {
        return actions;
    }
    actions.relinquish = true;
    actions.release_keyboard = keyboard;
    actions.release_mouse = mouse;
    return actions;
}

// How many keyboard interfaces are currently delivering reports, and how many of
// those accepted a non-zero SET_IDLE duration.
struct t_usb_hid_keyboard_idle_state
{
    int interfaces;
    int interfaces_periodic;
};

static inline void usb_hid_keyboard_idle_add(t_usb_hid_keyboard_idle_state& state, bool periodic)
{
    state.interfaces++;
    if (periodic) {
        state.interfaces_periodic++;
    }
}

static inline void usb_hid_keyboard_idle_remove(t_usb_hid_keyboard_idle_state& state, bool periodic)
{
    if (state.interfaces > 0) {
        state.interfaces--;
    }
    if (periodic && (state.interfaces_periodic > 0)) {
        state.interfaces_periodic--;
    }
}

// Keyboard_USB sees one merged report stream, so with two keyboards the periodic
// reports of one keep a stale key of the other looking fresh. The period is passed
// on only for a single attached keyboard that accepted the rate.
static inline int usb_hid_keyboard_idle_period_ms(const t_usb_hid_keyboard_idle_state& state)
{
    return ((state.interfaces == 1) && (state.interfaces_periodic == 1)) ? USB_HID_SET_IDLE_PERIOD_MS : 0;
}

#endif
