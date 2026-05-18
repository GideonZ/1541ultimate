#include "joystick_output.h"
#include <string.h>

#if U64
#include "FreeRTOS.h"
#if !RECOVERYAPP
#include "timers.h"
#endif
#include "u64.h"

#ifndef portENTER_CRITICAL
#define portENTER_CRITICAL()
#define portEXIT_CRITICAL()
#endif
#endif

#if U64 && !RECOVERYAPP
static const uint32_t JOYSTICK_REST_TIMER_TICKS = (pdMS_TO_TICKS(20) > 0) ? pdMS_TO_TICKS(20) : 1;

static void joystick_overlay_timer(TimerHandle_t timer)
{
    (void)timer;
    JoystickOutput::instance().tickOverlays();
}
#endif

JoystickOutput :: JoystickOutput()
{
    usb_p1 = 0x1F;
    rest_p1_persistent = 0x1F;
    rest_p2_persistent = 0x1F;
    rest_p1_overlay = 0x1F;
    rest_p2_overlay = 0x1F;
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

bool JoystickOutput :: port2Supported(void)
{
    return true;
}

void JoystickOutput :: apply(void)
{
#if U64
    C64_JOY1_SWOUT = ((usb_p1 & rest_p1_persistent & rest_p1_overlay) & 0x1F) | 0xE0;
    C64_JOY2_SWOUT = ((0x1F & rest_p2_persistent & rest_p2_overlay) & 0x1F) | 0xE0;
#endif
}

void JoystickOutput :: setUsbPort1(uint8_t active_low_mask)
{
#if U64
    portENTER_CRITICAL();
#endif
    usb_p1 = active_low_mask & 0x1F;
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
    rest_p1_persistent = active_low_mask & 0x1F;
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
    rest_p2_persistent = active_low_mask & 0x1F;
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: restPersistentSnapshot(uint8_t &port1_active_low, uint8_t &port2_active_low) const
{
    port1_active_low = rest_p1_persistent & 0x1F;
    port2_active_low = rest_p2_persistent & 0x1F;
}

static void arm_overlay_bits(uint8_t &overlay, uint8_t hold_state[5], uint8_t active_low_mask, const uint8_t hold[5])
{
    active_low_mask &= 0x1F;
    for (int i = 0; i < 5; i++) {
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

void JoystickOutput :: armRestPort1Overlay(uint8_t active_low_mask, const uint8_t hold[5])
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

void JoystickOutput :: armRestPort2Overlay(uint8_t active_low_mask, const uint8_t hold[5])
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

static bool tick_overlay_bits(uint8_t &overlay, uint8_t hold_state[5])
{
    bool changed = false;
    for (int i = 0; i < 5; i++) {
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
    rest_p1_persistent = 0x1F;
    rest_p2_persistent = 0x1F;
    rest_p1_overlay = 0x1F;
    rest_p2_overlay = 0x1F;
    memset(rest_p1_hold, 0, sizeof(rest_p1_hold));
    memset(rest_p2_hold, 0, sizeof(rest_p2_hold));
    apply();
#if U64
    portEXIT_CRITICAL();
#endif
}

void JoystickOutput :: snapshot(uint8_t &port1_active_low, uint8_t &port2_active_low) const
{
    port1_active_low = (rest_p1_persistent & rest_p1_overlay) & 0x1F;
    port2_active_low = (rest_p2_persistent & rest_p2_overlay) & 0x1F;
}
