#include "joystick_output.h"
#include <string.h>

#if U64
#include "FreeRTOS.h"
#if !RECOVERYAPP
#include "timers.h"
#include "itu.h"
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
// Runs every tick while the POT lines are behind the mouse.
static TimerHandle_t joystick_mouse_timer = NULL;

// The pacer needs the millisecond clock: a 5ms tick cannot tell a change 20ms
// after the previous one from one 24ms after it. It is read in the same
// critical section that writes the POT lines.
static uint16_t joystick_now(void)
{
    return getMsTimer();
}

static void joystick_mouse_timer_start(bool start)
{
    if (joystick_mouse_timer && (start != (xTimerIsTimerActive(joystick_mouse_timer) != pdFALSE))) {
        if (start) {
            xTimerStart(joystick_mouse_timer, 0);
        } else {
            xTimerStop(joystick_mouse_timer, 0);
        }
    }
}
#else
static uint16_t joystick_now(void)
{
    return 0;
}

static void joystick_mouse_timer_start(bool start)
{
    (void)start;
}
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

static void joystick_mouse_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    JoystickOutput::instance().tickMouse();
}
#endif

JoystickOutput :: JoystickOutput()
{
    usb_p1 = JOYSTICK_INPUT_MASK;
    rest_mouse_p1 = JOYSTICK_INPUT_MASK;
    mouse_active = false;
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
    joystick_mouse_timer = xTimerCreate("MousePace", 1, pdTRUE, NULL, joystick_mouse_timer_callback);
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
    if (usb_hid_get_active_mouse_interfaces) {
        mouse_port1_enabled = usb_hid_get_active_mouse_interfaces() > 0;
    }
    bool port1_extra_button = joystick_has_extra_button_press(usb_p1 & rest_mouse_p1 & rest_p1_persistent & rest_p1_overlay);
    C64_JOY1_SWOUT = port1 | 0xE0;
    C64_JOY2_SWOUT = port2 | 0xE0;
    C64_PADDLE_1_X = pot1x;
    C64_PADDLE_1_Y = pot1y;
    C64_PADDLE_2_X = pot2x;
    C64_PADDLE_2_Y = pot2y;
    C64_MOUSE_EN_1 = (mouse_port1_enabled || port1_extra_button) ? 1 : 0;
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

void JoystickOutput :: setRestMousePort1(uint8_t active_low_mask)
{
#if U64
    portENTER_CRITICAL();
#endif
    rest_mouse_p1 = (active_low_mask & JOYSTICK_DIGITAL_MASK) | (JOYSTICK_INPUT_MASK & ~JOYSTICK_DIGITAL_MASK);
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: setMousePosition(int16_t x, int16_t y)
{
#if U64
    portENTER_CRITICAL();
#endif
    uint16_t now = joystick_now();
    if (!mouse_active) {
        mouse_active = true;
        mouse_pacer.reset(x, y, now);
    } else {
        mouse_pacer.setTarget(x, y);
        mouse_pacer.advance(now);
    }
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: paceMouse(void)
{
#if U64
    portENTER_CRITICAL();
#endif
    bool behind = mouse_active && mouse_pacer.isBehind();
#if U64
    portEXIT_CRITICAL();
#endif
    if (behind) {
        joystick_mouse_timer_start(true);
    }
}

void JoystickOutput :: clearMousePosition(void)
{
#if U64
    portENTER_CRITICAL();
#endif
    mouse_active = false;
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: tickMouse(void)
{
#if U64
    portENTER_CRITICAL();
#endif
    if (mouse_active && mouse_pacer.advance(joystick_now())) {
        apply();
    }
    bool behind = mouse_active && mouse_pacer.isBehind();
#if U64
    portEXIT_CRITICAL();
#endif
    if (!behind) {
        joystick_mouse_timer_start(false);
    }
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

static void arm_overlay_bits(uint8_t &overlay, uint8_t hold_state[JOYSTICK_BUTTON_COUNT], uint8_t active_low_mask,
    const uint8_t hold[JOYSTICK_BUTTON_COUNT])
{
    active_low_mask &= JOYSTICK_INPUT_MASK;
    for (int i = 0; i < JOYSTICK_BUTTON_COUNT; i++) {
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

void JoystickOutput :: armRestPort1Overlay(uint8_t active_low_mask, const uint8_t hold[JOYSTICK_BUTTON_COUNT])
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

void JoystickOutput :: armRestPort2Overlay(uint8_t active_low_mask, const uint8_t hold[JOYSTICK_BUTTON_COUNT])
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

// Forces the masked bits' overlay back to released and cancels their hold
// countdown; other bits' overlays and countdowns are untouched.
static void cancel_overlay_bits(uint8_t &overlay, uint8_t hold_state[JOYSTICK_BUTTON_COUNT], uint8_t mask)
{
    mask &= JOYSTICK_INPUT_MASK;
    for (int i = 0; i < JOYSTICK_BUTTON_COUNT; i++) {
        if (mask & (1 << i)) {
            hold_state[i] = 0;
            overlay |= (1 << i);
        }
    }
}

void JoystickOutput :: cancelRestPort1Overlay(uint8_t mask)
{
#if U64
    portENTER_CRITICAL();
#endif
    cancel_overlay_bits(rest_p1_overlay, rest_p1_hold, mask);
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: cancelRestPort2Overlay(uint8_t mask)
{
#if U64
    portENTER_CRITICAL();
#endif
    cancel_overlay_bits(rest_p2_overlay, rest_p2_hold, mask);
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

static bool tick_overlay_bits(uint8_t &overlay, uint8_t hold_state[JOYSTICK_BUTTON_COUNT])
{
    bool changed = false;
    for (int i = 0; i < JOYSTICK_BUTTON_COUNT; i++) {
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
#if U64
    portENTER_CRITICAL();
#endif
    port1_active_low = (rest_p1_persistent & rest_p1_overlay) & JOYSTICK_INPUT_MASK;
    port2_active_low = (rest_p2_persistent & rest_p2_overlay) & JOYSTICK_INPUT_MASK;
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: outputSnapshot(uint8_t &port1_active_low, uint8_t &port2_active_low,
    uint8_t &port1_potx, uint8_t &port1_poty, uint8_t &port2_potx, uint8_t &port2_poty) const
{
    uint8_t port1 = usb_p1 & rest_mouse_p1 & rest_p1_persistent & rest_p1_overlay;
    uint8_t port2 = rest_p2_persistent & rest_p2_overlay;
    port1_active_low = port1 & JOYSTICK_DIGITAL_MASK;
    port2_active_low = port2 & JOYSTICK_DIGITAL_MASK;
    // A REST fire2 or fire3 press owns both POT lines while held. Otherwise a
    // mouse keeps its position on them through every port 1 line change (#909).
    if (mouse_active && !joystick_has_extra_button_press(port1)) {
        port1_potx = mouse_pacer.potX();
        port1_poty = mouse_pacer.potY();
    } else {
        port1_potx = joystick_potx_value(port1);
        port1_poty = joystick_poty_value(port1);
    }
    port2_potx = joystick_potx_value(port2);
    port2_poty = joystick_poty_value(port2);
}
