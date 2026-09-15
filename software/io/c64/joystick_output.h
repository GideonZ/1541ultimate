#ifndef JOYSTICK_OUTPUT_H
#define JOYSTICK_OUTPUT_H

#include "integer.h"
#include "mouse_pot_pacer.h"

// Must match INPUT_API_MAX_JOYSTICK_INPUTS in software/api/input_api.h.
static const int JOYSTICK_BUTTON_COUNT = 7;

class JoystickOutput
{
    uint8_t usb_p1;
    uint8_t rest_mouse_p1;      // lines of the REST mouse, a source of its own
    // The 1351 position of a USB mouse on port 1. apply() writes the POT
    // registers from what the pacer shows, so every port 1 update keeps the
    // mouse position, and fast movement cannot turn around between frames.
    bool usb_p1_mouse;
    const void *usb_p1_source;  // the mouse that reported last
    MousePotPacer usb_p1_pacer;
    uint8_t rest_p1_persistent;
    uint8_t rest_p2_persistent;
    uint8_t rest_p1_overlay;
    uint8_t rest_p2_overlay;
    uint8_t rest_p1_hold[JOYSTICK_BUTTON_COUNT];
    uint8_t rest_p2_hold[JOYSTICK_BUTTON_COUNT];

    JoystickOutput();
    void apply(void);

public:
    static JoystickOutput &instance();

    void setUsbPort1(uint8_t active_low_mask);
    void setRestMousePort1(uint8_t active_low_mask);
    // x and y are the running position of the mouse `source` in counts; only
    // their low seven bits reach the POT lines, paced by MousePotPacer.
    void setUsbPort1Mouse(int16_t x, int16_t y, const void *source);
    void clearUsbPort1Mouse(void);
    // Whether the POT lines show all of the mouse movement given so far. The
    // REST mouse sends its next report only then, so a path is never dropped.
    bool mouseSettled(void) const;
    // Shows more of the mouse movement, as far as the pacer's budget allows.
    // Called by the pacing timer; `now` is the millisecond clock.
    void tickMouse(uint16_t now);
    // The frame rate of the machine, which sets how far apart two changes of
    // the mouse position on the POT lines must be to reach separate frames.
    void setMouseFrameRate(int hertz);

    void setRestPort1Persistent(uint8_t active_low_mask);
    void setRestPort2Persistent(uint8_t active_low_mask);
    void restPersistentSnapshot(uint8_t &port1_active_low, uint8_t &port2_active_low) const;

    void armRestPort1Overlay(uint8_t active_low_mask, const uint8_t hold[JOYSTICK_BUTTON_COUNT]);
    void armRestPort2Overlay(uint8_t active_low_mask, const uint8_t hold[JOYSTICK_BUTTON_COUNT]);

    // Cancels an in-flight tap for the given bits so a release isn't masked.
    void cancelRestPort1Overlay(uint8_t mask);
    void cancelRestPort2Overlay(uint8_t mask);

    void tickOverlays(void);

    void releaseAllRest(void);

    void snapshot(uint8_t &port1_active_low, uint8_t &port2_active_low) const;
    void outputSnapshot(uint8_t &port1_active_low, uint8_t &port2_active_low,
        uint8_t &port1_potx, uint8_t &port1_poty, uint8_t &port2_potx, uint8_t &port2_poty) const;

};

#endif /* JOYSTICK_OUTPUT_H */
