#ifndef JOYSTICK_OUTPUT_H
#define JOYSTICK_OUTPUT_H

#include "integer.h"

// Must match INPUT_API_MAX_JOYSTICK_INPUTS in software/api/input_api.h.
static const int JOYSTICK_BUTTON_COUNT = 7;

class JoystickOutput
{
    uint8_t usb_p1;
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
