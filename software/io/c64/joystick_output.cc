#include "joystick_output.h"
#include <string.h>

#if U64
#include "FreeRTOS.h"
#if !RECOVERYAPP
#include "timers.h"
#endif
#include "u64.h"
extern "C" int usb_hid_get_active_mouse_interfaces(void) __attribute__((weak));

#ifndef portENTER_CRITICAL
#define portENTER_CRITICAL()
#define portEXIT_CRITICAL()
#endif
#endif

#if U64 && !RECOVERYAPP
static const uint32_t JOYSTICK_REST_TIMER_TICKS = (pdMS_TO_TICKS(20) > 0) ? pdMS_TO_TICKS(20) : 1;
#endif

static const uint8_t JOYSTICK_DIGITAL_MASK = 0x1F;
static const uint8_t JOYSTICK_FIRE2_BIT = (1 << 5);
static const uint8_t JOYSTICK_FIRE3_BIT = (1 << 6);
static const uint8_t JOYSTICK_INPUT_MASK = JOYSTICK_DIGITAL_MASK | JOYSTICK_FIRE2_BIT | JOYSTICK_FIRE3_BIT;
static const uint8_t JOYSTICK_POT_RELEASED = 0x80;
static const uint8_t JOYSTICK_POT_PRESSED = 0x00;

static uint8_t joystick_potx_value(uint8_t active_low_mask)
{
    return (active_low_mask & JOYSTICK_FIRE2_BIT) ? JOYSTICK_POT_RELEASED : JOYSTICK_POT_PRESSED;
}

static uint8_t joystick_poty_value(uint8_t active_low_mask)
{
    return (active_low_mask & JOYSTICK_FIRE3_BIT) ? JOYSTICK_POT_RELEASED : JOYSTICK_POT_PRESSED;
}

static bool joystick_has_extra_button_press(uint8_t active_low_mask)
{
    return (active_low_mask & (JOYSTICK_FIRE2_BIT | JOYSTICK_FIRE3_BIT)) != (JOYSTICK_FIRE2_BIT | JOYSTICK_FIRE3_BIT);
}

#if U64 && !RECOVERYAPP
static void joystick_overlay_timer(TimerHandle_t timer)
{
    (void)timer;
    JoystickOutput::instance().tickOverlays();
}
#endif

JoystickOutput :: JoystickOutput()
{
    usb_p1 = JOYSTICK_INPUT_MASK;
    rest_p1_persistent = JOYSTICK_INPUT_MASK;
    rest_p2_persistent = JOYSTICK_INPUT_MASK;
    rest_p1_overlay = JOYSTICK_INPUT_MASK;
    rest_p2_overlay = JOYSTICK_INPUT_MASK;
    memset(rest_p1_hold, 0, sizeof(rest_p1_hold));
    memset(rest_p2_hold, 0, sizeof(rest_p2_hold));
#if U64 && !RECOVERYAPP
    TimerHandle_t timer = xTimerCreate("RestJoy", JOYSTICK_REST_TIMER_TICKS, pdTRUE, NULL, joystick_overlay_timer);
    if (timer) {
        xTimerStart(timer, 0);
    }
#endif
}

JoystickOutput &JoystickOutput :: instance()
{
    static JoystickOutput output;
    return output;
}

void JoystickOutput :: apply(void)
{
#if U64
    uint8_t port1, port2, pot1x, pot1y, pot2x, pot2y;
    bool mouse_port1_enabled = false;
    outputSnapshot(port1, port2, pot1x, pot1y, pot2x, pot2y);
    C64_JOY1_SWOUT = port1 | 0xE0;
    C64_JOY2_SWOUT = port2 | 0xE0;
    C64_PADDLE_1_X = pot1x;
    C64_PADDLE_1_Y = pot1y;
    C64_PADDLE_2_X = pot2x;
    C64_PADDLE_2_Y = pot2y;
    if (usb_hid_get_active_mouse_interfaces) {
        mouse_port1_enabled = usb_hid_get_active_mouse_interfaces() > 0;
    }
    C64_MOUSE_EN_1 = (mouse_port1_enabled || joystick_has_extra_button_press(usb_p1 & rest_p1_persistent & rest_p1_overlay)) ? 1 : 0;
    C64_MOUSE_EN_2 = joystick_has_extra_button_press(rest_p2_persistent & rest_p2_overlay) ? 1 : 0;
#endif
}

void JoystickOutput :: setUsbPort1(uint8_t active_low_mask)
{
#if U64
    portENTER_CRITICAL();
#endif
    usb_p1 = (active_low_mask & JOYSTICK_DIGITAL_MASK) | (JOYSTICK_INPUT_MASK & ~JOYSTICK_DIGITAL_MASK);
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: setRestPort1Persistent(uint8_t active_low_mask)
{
#if U64
    portENTER_CRITICAL();
#endif
    rest_p1_persistent = active_low_mask & JOYSTICK_INPUT_MASK;
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: setRestPort2Persistent(uint8_t active_low_mask)
{
#if U64
    portENTER_CRITICAL();
#endif
    rest_p2_persistent = active_low_mask & JOYSTICK_INPUT_MASK;
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: restPersistentSnapshot(uint8_t &port1_active_low, uint8_t &port2_active_low) const
{
    port1_active_low = rest_p1_persistent & JOYSTICK_INPUT_MASK;
    port2_active_low = rest_p2_persistent & JOYSTICK_INPUT_MASK;
}

static void arm_overlay_bits(uint8_t &overlay, uint8_t hold_state[7], uint8_t active_low_mask, const uint8_t hold[7])
{
    active_low_mask &= JOYSTICK_INPUT_MASK;
    for (int i = 0; i < 7; i++) {
        uint8_t bit = (1 << i);
        if (hold[i] != 0) {
            hold_state[i] = hold[i];
            if (active_low_mask & bit) {
                overlay |= bit;
            } else {
                overlay &= ~bit;
            }
        }
    }
}

void JoystickOutput :: armRestPort1Overlay(uint8_t active_low_mask, const uint8_t hold[7])
{
#if U64
    portENTER_CRITICAL();
#endif
    arm_overlay_bits(rest_p1_overlay, rest_p1_hold, active_low_mask, hold);
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: armRestPort2Overlay(uint8_t active_low_mask, const uint8_t hold[7])
{
#if U64
    portENTER_CRITICAL();
#endif
    arm_overlay_bits(rest_p2_overlay, rest_p2_hold, active_low_mask, hold);
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

static bool tick_overlay_bits(uint8_t &overlay, uint8_t hold_state[7])
{
    bool changed = false;
    for (int i = 0; i < 7; i++) {
        if (hold_state[i] == 0) {
            continue;
        }
        hold_state[i]--;
        if (hold_state[i] == 0) {
            overlay |= (1 << i);
            changed = true;
        }
    }
    return changed;
}

void JoystickOutput :: tickOverlays(void)
{
#if U64
    portENTER_CRITICAL();
#endif
    bool changed = tick_overlay_bits(rest_p1_overlay, rest_p1_hold);
    changed |= tick_overlay_bits(rest_p2_overlay, rest_p2_hold);
    if (changed) {
        apply();
    }
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: releaseAllRest(void)
{
#if U64
    portENTER_CRITICAL();
#endif
    rest_p1_persistent = JOYSTICK_INPUT_MASK;
    rest_p2_persistent = JOYSTICK_INPUT_MASK;
    rest_p1_overlay = JOYSTICK_INPUT_MASK;
    rest_p2_overlay = JOYSTICK_INPUT_MASK;
    memset(rest_p1_hold, 0, sizeof(rest_p1_hold));
    memset(rest_p2_hold, 0, sizeof(rest_p2_hold));
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: snapshot(uint8_t &port1_active_low, uint8_t &port2_active_low) const
{
    port1_active_low = (rest_p1_persistent & rest_p1_overlay) & JOYSTICK_INPUT_MASK;
    port2_active_low = (rest_p2_persistent & rest_p2_overlay) & JOYSTICK_INPUT_MASK;
}

void JoystickOutput :: outputSnapshot(uint8_t &port1_active_low, uint8_t &port2_active_low,
    uint8_t &port1_potx, uint8_t &port1_poty, uint8_t &port2_potx, uint8_t &port2_poty) const
{
    uint8_t port1 = usb_p1 & rest_p1_persistent & rest_p1_overlay;
    uint8_t port2 = rest_p2_persistent & rest_p2_overlay;
    port1_active_low = port1 & JOYSTICK_DIGITAL_MASK;
    port2_active_low = port2 & JOYSTICK_DIGITAL_MASK;
    port1_potx = joystick_potx_value(port1);
    port1_poty = joystick_poty_value(port1);
    port2_potx = joystick_potx_value(port2);
    port2_poty = joystick_poty_value(port2);
}
