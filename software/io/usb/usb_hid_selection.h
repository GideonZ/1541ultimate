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

// SET_IDLE counts its duration in units of 4ms, so 25 asks a HID keyboard to
// re-send its report every 100ms for as long as a key stays down. Keyboard_USB
// needs those refreshes to tell a held key from a release report that went
// missing; with the idle rate at 0 a keyboard reports only on change and there
// is nothing to time out against. 100ms sits well inside the 320ms the menu
// takes to start repeating, so a lost release is caught before the repeat can
// emit anything, and it costs one extra 8 byte report per five polls of an
// endpoint that is already polled at 50Hz. A shorter period would tighten that
// margin further, but it multiplies the work done on the single USB event task
// whose congestion is what delays a release report in the first place.
static const uint8_t USB_HID_SET_IDLE_UNITS = 25;
static const int USB_HID_SET_IDLE_PERIOD_MS = 4 * USB_HID_SET_IDLE_UNITS;

// The SET_IDLE duration to request for an interface that was selected as the
// given combination of keyboard and mouse. The request carries report id 0,
// which addresses every input report of the interface, so a non-zero duration
// on an interface that also delivers mouse reports would make a motionless
// mouse send a report every period. The HID specification recommends an
// infinite idle period, that is duration 0, for mice and joysticks, and the
// extra traffic would land on the same USB event task whose congestion delays
// the keyboard reports this feature depends on. Only a keyboard-only interface
// is therefore asked for a periodic rate; a mouse-only or a composite
// keyboard/mouse interface keeps the previous duration of 0.
static inline uint8_t usb_hid_set_idle_units(bool keyboard, bool mouse)
{
    return (keyboard && !mouse) ? USB_HID_SET_IDLE_UNITS : 0;
}

// Whether the keyboard stored the requested idle rate. A keyboard can acknowledge
// SET_IDLE without storing the duration, and control_exchange() returns the number
// of bytes transferred, which is 0 for a request without a data stage whether or
// not its status phase succeeded. Neither result proves that periodic reporting is
// on, so the rate counts as accepted only when a following GET_IDLE reads back
// exactly the duration that was requested. A keyboard that stalls GET_IDLE keeps
// the previous unbounded repeat behaviour.
static inline bool usb_hid_idle_rate_accepted(uint8_t requested_units,
                                              int get_idle_result,
                                              uint8_t reported_units)
{
    return (requested_units != 0) && (get_idle_result == 1) && (reported_units == requested_units);
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

// The idle period Keyboard_USB may time its repeat against. Every keyboard
// interface feeds the same merged report stream, and Keyboard_USB cannot tell
// which interface a report came from. With two keyboards attached, the periodic
// reports of one keyboard keep the merged report looking fresh while the other
// keyboard is silent, so a lost release on the silent keyboard would never reach
// the staleness ceiling. A keyboard that ignored SET_IDLE is silent while a key
// is held, and a ceiling would cut its auto-repeat off. The period is therefore
// only passed on when exactly one keyboard interface is attached and that
// interface accepted the periodic rate; in every other case the previous
// unbounded repeat behaviour is kept.
static inline int usb_hid_keyboard_idle_period_ms(const t_usb_hid_keyboard_idle_state& state)
{
    return ((state.interfaces == 1) && (state.interfaces_periodic == 1)) ? USB_HID_SET_IDLE_PERIOD_MS : 0;
}

#endif
