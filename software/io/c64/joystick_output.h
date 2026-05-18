#ifndef JOYSTICK_OUTPUT_H
#define JOYSTICK_OUTPUT_H

#include "integer.h"

class JoystickOutput
{
    uint8_t usb_p1;
    uint8_t rest_p1_persistent;
    uint8_t rest_p2_persistent;
    uint8_t rest_p1_overlay;
    uint8_t rest_p2_overlay;
    uint8_t rest_p1_hold[5];
    uint8_t rest_p2_hold[5];

    JoystickOutput();
    void apply(void);

public:
    static JoystickOutput &instance();

    void setUsbPort1(uint8_t active_low_mask);

    void setRestPort1Persistent(uint8_t active_low_mask);
    void setRestPort2Persistent(uint8_t active_low_mask);
    void restPersistentSnapshot(uint8_t &port1_active_low, uint8_t &port2_active_low) const;

    void armRestPort1Overlay(uint8_t active_low_mask, const uint8_t hold[5]);
    void armRestPort2Overlay(uint8_t active_low_mask, const uint8_t hold[5]);

    void tickOverlays(void);

    void releaseAllRest(void);

    void snapshot(uint8_t &port1_active_low, uint8_t &port2_active_low) const;

    static bool port2Supported(void);
};

#endif /* JOYSTICK_OUTPUT_H */
