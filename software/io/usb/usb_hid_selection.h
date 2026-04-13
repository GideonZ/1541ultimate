#ifndef USB_HID_SELECTION_H
#define USB_HID_SELECTION_H

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

#endif
