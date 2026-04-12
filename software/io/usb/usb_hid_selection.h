#ifndef USB_HID_SELECTION_H
#define USB_HID_SELECTION_H

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
