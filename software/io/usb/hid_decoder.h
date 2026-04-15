/*
 * hid_decoder.h
 *
 *  Created on: Feb 19, 2017
 *      Author: Gideon
 */

#ifndef _HID_DECODER_H
#define _HID_DECODER_H

#include "fifo.h"
#include "indexed_list.h"
#include "stack.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SIZE_0 0x00
#define SIZE_1 0x01
#define SIZE_2 0x02
#define SIZE_4 0x03
#define SIZE_MASK 0x03

#define TYPE_MAIN 0x00
#define TYPE_GLOBAL 0x04
#define TYPE_LOCAL 0x08
#define TYPE_MASK 0x0C
#define ITEM_MASK 0xFC

#define MAIN_INPUT 0x80
#define MAIN_OUTPUT 0x90
#define MAIN_FEATURE 0xB0
#define MAIN_COLLECTION 0xA0
#define MAIN_END_COLL 0xC0

#define GLOBAL_USAGE_PAGE 0x04
#define GLOBAL_LOGICAL_MIN 0x14
#define GLOBAL_LOGICAL_MAX 0x24
#define GLOBAL_PHYSICAL_MIN 0x34
#define GLOBAL_PHYSICAL_MAX 0x44
#define GLOBAL_UNIT_EXP 0x54
#define GLOBAL_UNIT 0x64
#define GLOBAL_REPORT_SIZE 0x74
#define GLOBAL_REPORT_ID 0x84
#define GLOBAL_REPORT_COUNT 0x94
#define GLOBAL_PUSH 0xA4
#define GLOBAL_POP 0xB4

#define LOCAL_USAGE 0x08
#define LOCAL_USAGE_MIN 0x18
#define LOCAL_USAGE_MAX 0x28
#define LOCAL_DESIGNATOR_IDX 0x38
#define LOCAL_DESIGNATOR_MIN 0x48
#define LOCAL_DESIGNATOR_MAX 0x58
#define LOCAL_STRING_IDX 0x78
#define LOCAL_STRING_MIN 0x88
#define LOCAL_STRING_MAX 0x98
#define LOCAL_DELIMITER 0xA8

#define FLAGS_CONSTANT 0x01
#define FLAGS_VARIABLE 0x02
#define FLAGS_RELATIVE 0x04
// ...

#define COLLECTION_PHYSICAL 0x00
#define COLLECTION_APPLICATION 0x01
#define COLLECTION_LOGICAL 0x02

typedef struct
{
    uint32_t usage_page;
    int logical_minimum;
    int logical_maximum;
    int physical_minimum;
    int physical_maximum;
    int unit_exp;
    int unit;
    uint32_t report_size;
    uint32_t report_id;
    uint32_t report_count;
} t_globals;

typedef struct
{
    uint32_t usage;
    uint32_t usage_min;
    uint32_t usage_max;
    uint32_t designator_idx;
    uint32_t designator_min;
    uint32_t designator_max;
    uint32_t string_idx;
    uint32_t string_min;
    uint32_t string_max;
} t_locals;

typedef struct
{
    uint32_t usage;
    uint32_t flags;
    uint32_t length;
    uint32_t count;
    uint32_t min, max;
} t_reportItem;

typedef struct {
    int offset;
    int length;
    int count;
    int min, max;
    uint8_t flags;
    uint8_t report;
} t_item_location;

typedef struct {
    bool valid;
    bool has_button1;
    bool has_button2;
    bool has_button3;
    bool has_wheel_v;
    bool has_wheel_h;
    t_item_location button1;
    t_item_location button2;
    t_item_location button3;
    t_item_location mouse_x;
    t_item_location mouse_y;
    t_item_location wheel_v;
    t_item_location wheel_h;
} t_hid_mouse_fields;

typedef struct {
    bool valid;
    bool has_modifier[8];
    bool has_key_array;
    bool has_bitmap_keys;
    t_item_location modifiers[8];
    t_item_location key_array;
} t_hid_keyboard_fields;

typedef struct {
    uint8_t buttons;
    int x;
    int y;
} t_hid_boot_mouse_sample;

class UsageFifo
{
    Fifo<uint32_t> fifo;
    uint32_t lastUsage;

  public:
    UsageFifo() : fifo(256, 0) { lastUsage = 0; }

    uint32_t pop()
    {
        if (!fifo.is_empty()) {
            lastUsage = fifo.pop();
        }
        return lastUsage;
    }
    void push(uint32_t value) { fifo.push(value); }
    bool is_empty() { return fifo.is_empty(); }

    uint32_t head()
    {
        if (!fifo.is_empty()) {
            return fifo.head();
        }
        return lastUsage;
    }
};

class HidReport
{
  public:
    uint32_t applicationUsage;
    uint32_t reportId;
    IndexedList<t_reportItem *> items;
    HidReport(uint32_t u, uint32_t id) : items(16, NULL)
    {
        applicationUsage = u;
        reportId = id;
    }
    ~HidReport()
    {
        for (int i = 0; i < items.get_elements(); i++) {
            delete items[i];
        }
    }
    void addItem(uint32_t usage, uint32_t flags, uint32_t length, uint32_t count, uint32_t min, uint32_t max)
    {
        t_reportItem *t = new t_reportItem;
        t->usage = usage;
        t->flags = flags;
        t->length = length;
        t->count = count;
        t->min = min;
        t->max = max;
        items.append(t);
    }
    char *getFlagsString(char *buffer, t_reportItem *t)
    {
        if (t->flags & FLAGS_CONSTANT) {
            strcpy(buffer, "Constant");
        } else if (t->flags & FLAGS_VARIABLE) {
            strcpy(buffer, "Variable");
        } else {
            strcpy(buffer, "Array");
        }
        strcat(buffer, (t->flags & FLAGS_RELATIVE) ? ", Relative" : ", Absolute");
        return buffer;
    }

    static bool reportMatches(const uint8_t *data, const t_item_location& loc)
    {
        return (loc.report == 0) || (data[0] == loc.report);
    }

    static int getRequiredBytes(const t_item_location& loc)
    {
        return (loc.offset + loc.length + 7) >> 3;
    }

    static bool hasValue(const uint8_t *data, int data_len, const t_item_location& loc)
    {
        if (data_len <= 0) {
            return false;
        }
        if (!reportMatches(data, loc)) {
            return false;
        }
        return data_len >= getRequiredBytes(loc);
    }

    static int getValueFromData(const uint8_t *data, const t_item_location& loc)
    {
        if (!reportMatches(data, loc)) {
            return 0;
        }
        int first_byte = loc.offset >> 3;
        int last_byte = (loc.offset + loc.length - 1) >> 3;
        int shift = loc.offset & 7;
        int result;
        // fits in any byte
        if (first_byte == last_byte) {
            result = (data[first_byte] >> shift);
        } else {
            uint32_t longer;
            const uint8_t *pnt = data + first_byte;
            longer = *(pnt++);
            longer |= uint32_t(*(pnt++)) << 8;
            longer |= uint32_t(*(pnt++)) << 16;
            longer |= uint32_t(*(pnt++)) << 24;
            longer >>= shift;
            result = (int)longer;
        }
        result &= ((1 << loc.length) - 1); // mask properly
        if (loc.min < 0) { // signed
            if (result & (1 << (loc.length-1))) { // upper bit set
                result -= (1 << loc.length);
            }
        }
        return result;
    }

    static int getValueFromData(const uint8_t *data, int data_len, const t_item_location& loc)
    {
        if (!hasValue(data, data_len, loc)) {
            return 0;
        }
        return getValueFromData(data, loc);
    }

    static int getArrayValueFromData(const uint8_t *data, const t_item_location& loc, int index)
    {
        if ((index < 0) || (index >= loc.count)) {
            return 0;
        }
        t_item_location entry = loc;
        entry.count = 1;
        entry.offset += index * loc.length;
        return getValueFromData(data, entry);
    }

    static int getArrayValueFromData(const uint8_t *data, int data_len, const t_item_location& loc, int index)
    {
        if ((index < 0) || (index >= loc.count)) {
            return 0;
        }
        t_item_location entry = loc;
        entry.count = 1;
        entry.offset += index * loc.length;
        return getValueFromData(data, data_len, entry);
    }
};

class HidBootProtocol
{
  public:
    static void copyKeyboardReport(const uint8_t *data, int data_len, uint8_t out[8])
    {
        memset(out, 0, 8);
        if ((!data) || (data_len <= 0)) {
            return;
        }
        if (data_len > 8) {
            data_len = 8;
        }
        memcpy(out, data, data_len);
    }

    static void decodeMouseReport(const uint8_t *data, int data_len, t_hid_boot_mouse_sample& sample)
    {
        sample.buttons = 0;
        sample.x = 0;
        sample.y = 0;
        if ((!data) || (data_len <= 0)) {
            return;
        }
        sample.buttons = data[0] & 0x07;
        if (data_len > 1) {
            sample.x = (int8_t)data[1];
        }
        if (data_len > 2) {
            sample.y = (int8_t)data[2];
        }
    }
};

class HidMouseInterpreter
{
  public:
    enum {
        MOUSE_MODE_CURSOR = 0,
        MOUSE_MODE_MOUSE = 1,
        MOUSE_MODE_MOUSE_CURSOR = 2,
        MOUSE_MODE_MOUSE_WHEEL = 3
    };

    enum {
        WHEEL_PULSE_PHASE_IDLE = 0,
        WHEEL_PULSE_PHASE_LOW = 1,
        WHEEL_PULSE_PHASE_HIGH = 2
    };

    enum {
        SENSITIVITY_SCALE_STEP = 32,
        ADAPTIVE_ACCELERATION_TARGET_EMA = 32,
        ADAPTIVE_ACCELERATION_MAX_EMA = 96,
        ADAPTIVE_ACCELERATION_FALLBACK_SCALE = 384,
        ADAPTIVE_ACCELERATION_MIN_SCALE = 384,
        ADAPTIVE_ACCELERATION_MAX_SCALE = 576
    };

    static int divideRounded(int value, int divisor)
    {
        if (value >= 0) {
            return (value + (divisor / 2)) / divisor;
        }
        return -(((-value) + (divisor / 2)) / divisor);
    }

    static int scaleWheelWithRemainder(int wheel_delta, int multiplier, int divisor, int& remainder)
    {
        int scaled = (wheel_delta * multiplier) + remainder;
        int delta = scaled / divisor;
        remainder = scaled % divisor;
        return delta;
    }

    static int scaleFixedWithRemainder(int raw_delta, int scale_factor, int& remainder)
    {
        return scaleWheelWithRemainder(raw_delta, scale_factor, 256, remainder);
    }

    static int clampWheelSetting(int scroll_factor)
    {
        if (scroll_factor < 1) {
            return 1;
        }
        if (scroll_factor > 16) {
            return 16;
        }
        return scroll_factor;
    }

    static int clampSensitivity(int sensitivity)
    {
        if (sensitivity < 1) {
            return 1;
        }
        if (sensitivity > 16) {
            return 16;
        }
        return sensitivity;
    }

    static int computeSensitivityScaleFactor(int sensitivity)
    {
        return clampSensitivity(sensitivity) * SENSITIVITY_SCALE_STEP;
    }

    static int scaleSensitivity(int raw_delta, int sensitivity)
    {
        return scaleFixed(raw_delta, computeSensitivityScaleFactor(sensitivity));
    }

    static int scaleCursorMotionKeys(int motion_delta)
    {
        int magnitude = (motion_delta < 0) ? -motion_delta : motion_delta;
        if (magnitude == 0) {
            return 0;
        }
        int repeat = divideRounded(magnitude, 4);
        if (repeat == 0) {
            repeat = 1;
        }
        return clampDelta(repeat, 63);
    }

    static int normalizeCursorVerticalMotion(int motion_y)
    {
        return -motion_y;
    }

    static bool mouseModeRoutesMotionToCursor(int mouse_mode)
    {
        return mouse_mode == MOUSE_MODE_CURSOR;
    }

    static bool mouseModeRoutesMotionToPointer(int mouse_mode)
    {
        return mouse_mode != MOUSE_MODE_CURSOR;
    }

    static bool mouseModeRoutesWheelToCursor(int mouse_mode)
    {
        return (mouse_mode == MOUSE_MODE_CURSOR) || (mouse_mode == MOUSE_MODE_MOUSE_CURSOR);
    }

    static bool mouseModeRoutesWheelToPointer(int mouse_mode)
    {
        return mouse_mode == MOUSE_MODE_MOUSE;
    }

    static bool mouseModeRoutesWheelToNative(int mouse_mode)
    {
        return mouse_mode == MOUSE_MODE_MOUSE_WHEEL;
    }

    static int computeNativeWheelGain(int sensitivity)
    {
        // Mouse + Wheel mode emits discrete Micromys pulses, so map the
        // promoted detent directly to the configured sensitivity. This keeps
        // sensitivity 1 unchanged and lets higher settings expand a single
        // notch into multiple native wheel pulses.
        return clampWheelSetting(sensitivity);
    }

    static int computeNativeWheelThreshold(int sensitivity)
    {
        (void)sensitivity;
        return 8;
    }

    static int accumulateNativeWheelSteps(int wheel_delta, int sensitivity, int& accumulator)
    {
        int threshold = computeNativeWheelThreshold(sensitivity);
        int scaled_delta = wheel_delta * computeNativeWheelGain(sensitivity);
        int steps = 0;

        if (((wheel_delta > 0) && (accumulator < 0)) ||
            ((wheel_delta < 0) && (accumulator > 0))) {
            accumulator = 0;
        }

        accumulator += scaled_delta;
        while (accumulator >= threshold) {
            accumulator -= threshold;
            steps++;
        }
        while (accumulator <= -threshold) {
            accumulator += threshold;
            steps--;
        }
        return steps;
    }

    static void mergeNativeWheelBurst(int steps, uint8_t max_burst,
                                      int& burst_direction, uint8_t& burst_count)
    {
        int direction;
        int updated_count;

        if ((steps == 0) || (max_burst == 0)) {
            return;
        }

        direction = (steps > 0) ? 1 : -1;
        if (burst_direction != direction) {
            burst_direction = direction;
            burst_count = 0;
        }

        updated_count = burst_count + absoluteValue(steps);
        if (updated_count > max_burst) {
            updated_count = max_burst;
        }
        burst_count = (uint8_t)updated_count;
    }

    static bool advanceNativeWheelBurst(int& burst_direction, uint8_t& burst_count,
                                        int& phase, uint8_t& pulse_mask,
                                        uint32_t& next_tick, uint32_t now, uint32_t phase_ticks)
    {
        if (phase_ticks == 0) {
            phase_ticks = 1;
        }

        while (1) {
            if (phase == WHEEL_PULSE_PHASE_IDLE) {
                if ((burst_count > 0) && (burst_direction != 0)) {
                    pulse_mask = (burst_direction > 0) ? 0x04 : 0x08;
                    burst_count--;
                    phase = WHEEL_PULSE_PHASE_LOW;
                    next_tick = now + phase_ticks;
                    return true;
                }
                burst_direction = 0;
                pulse_mask = 0;
                return false;
            }

            if ((int32_t)(now - next_tick) < 0) {
                return true;
            }

            if (phase == WHEEL_PULSE_PHASE_LOW) {
                phase = WHEEL_PULSE_PHASE_HIGH;
                next_tick += phase_ticks;
                continue;
            }

            phase = WHEEL_PULSE_PHASE_IDLE;
        }
    }

    static uint8_t applyWheelPulseMask(uint8_t mouse_joy, int phase, uint8_t pulse_mask)
    {
        mouse_joy |= 0x0C;
        if ((phase == WHEEL_PULSE_PHASE_LOW) && pulse_mask) {
            mouse_joy &= (uint8_t)~pulse_mask;
        }
        return mouse_joy;
    }

    static int scaleWheelDelta(int wheel_delta, int scroll_factor,
                               int multiplier, int divisor, int& remainder)
    {
        if (wheel_delta == 0) {
            return 0;
        }
        return scaleWheelWithRemainder(wheel_delta, clampWheelSetting(scroll_factor) * multiplier, divisor, remainder);
    }

    static int clampDelta(int delta, int limit)
    {
        if (delta > limit) {
            return limit;
        }
        if (delta < -limit) {
            return -limit;
        }
        return delta;
    }

    static int absoluteValue(int value)
    {
        return (value < 0) ? -value : value;
    }

    static int updateMotionEma(int ema_x16, int motion_x, int motion_y, int divisor)
    {
        int magnitude_x16 = (absoluteValue(motion_x) + absoluteValue(motion_y)) << 4;
        ema_x16 += (magnitude_x16 - ema_x16) / divisor;
        return ema_x16;
    }

    static int updateAdaptiveAccelerationEma(int ema_x16, int motion_x, int motion_y)
    {
        return updateMotionEma(ema_x16, motion_x, motion_y, 4);
    }

    static int clampScaleFactor(int scale_factor, int minimum, int maximum)
    {
        if (scale_factor < minimum) {
            return minimum;
        }
        if (scale_factor > maximum) {
            return maximum;
        }
        return scale_factor;
    }

    static int computeAdaptiveAccelerationScale(int ema_x16)
    {
        if (ema_x16 <= 0) {
            return ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
        }
        int target_ema_x16 = ADAPTIVE_ACCELERATION_TARGET_EMA << 4;
        if (ema_x16 <= target_ema_x16) {
            return ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
        }
        int max_ema_x16 = ADAPTIVE_ACCELERATION_MAX_EMA << 4;
        if (ema_x16 >= max_ema_x16) {
            return ADAPTIVE_ACCELERATION_MAX_SCALE;
        }
        int excess = ema_x16 - target_ema_x16;
        int range = max_ema_x16 - target_ema_x16;
        int scale_range = ADAPTIVE_ACCELERATION_MAX_SCALE - ADAPTIVE_ACCELERATION_FALLBACK_SCALE;
        int curved_scale = ADAPTIVE_ACCELERATION_FALLBACK_SCALE +
                           (scale_range * excess * excess) / (range * range);
        return clampScaleFactor(curved_scale,
                                ADAPTIVE_ACCELERATION_MIN_SCALE,
                                ADAPTIVE_ACCELERATION_MAX_SCALE);
    }

    static int limitScaleStep(int previous_scale, int next_scale, int max_step)
    {
        if (next_scale > previous_scale + max_step) {
            return previous_scale + max_step;
        }
        if (next_scale < previous_scale - max_step) {
            return previous_scale - max_step;
        }
        return next_scale;
    }

    static int scaleFixed(int raw_delta, int scale_factor)
    {
        return (raw_delta * scale_factor) / 256;
    }

    static int normalizeHorizontalWheel(int wheel_h)
    {
        // AC Pan reports are opposite the existing right/left menu convention.
        return -wheel_h;
    }

    static int normalizeVerticalWheel(int wheel_v)
    {
        // Standard detented wheels often report one unit per notch, while
        // higher-resolution wheels report several smaller deltas over the same
        // physical motion. Promote single-detent reports to a canonical step so
        // both classes respond similarly at the same user sensitivity.
        if (wheel_v == 1) {
            return 8;
        }
        if (wheel_v == -1) {
            return -8;
        }
        return wheel_v;
    }

    static int applyWheelDirection(int wheel_delta, bool reversed)
    {
        return reversed ? -wheel_delta : wheel_delta;
    }

    static void applyRelativeMotion(int16_t& mouse_x, int16_t& mouse_y, int x, int y)
    {
        mouse_x += x;
        mouse_y -= y;
    }

    static int scaleHorizontalWheel(int wheel_h, int scroll_factor, int& remainder)
    {
        remainder = 0;
        return wheel_h * clampWheelSetting(scroll_factor) * 2;
    }

    static int scaleHorizontalWheel(int wheel_h, int scroll_factor)
    {
        int remainder = 0;
        return scaleHorizontalWheel(wheel_h, scroll_factor, remainder);
    }

    static int scaleHorizontalWheelAxisDelta(int wheel_h, int scroll_factor, int& remainder)
    {
        return clampDelta(scaleHorizontalWheel(wheel_h, scroll_factor, remainder), 63);
    }

    static int scaleVerticalWheel(int wheel_v, int scroll_factor, int& remainder)
    {
        return scaleWheelDelta(wheel_v, scroll_factor, 10, 32, remainder);
    }

    static int scaleVerticalWheelAxisDelta(int wheel_v, int scroll_factor, int& remainder)
    {
        return clampDelta(scaleVerticalWheel(wheel_v, scroll_factor, remainder), 63);
    }

    static int scaleHorizontalWheelKeys(int wheel_h, int scroll_factor, int& remainder)
    {
        remainder = 0;
        return divideRounded(wheel_h * clampWheelSetting(scroll_factor) * 3, 2);
    }

    static int scaleHorizontalWheelKeys(int wheel_h, int scroll_factor)
    {
        int remainder = 0;
        return scaleHorizontalWheelKeys(wheel_h, scroll_factor, remainder);
    }

    static int scaleVerticalWheelKeys(int wheel_v, int scroll_factor, int& remainder)
    {
        return scaleWheelDelta(wheel_v, scroll_factor, 1, 4, remainder);
    }

    static int scaleMenuWheelKeys(int wheel_delta)
    {
        if (wheel_delta > 0) {
            return 1;
        }
        if (wheel_delta < 0) {
            return -1;
        }
        return 0;
    }

    enum t_menu_wheel_mode {
        MENU_WHEEL_MODE_PRECISE = 0,
        MENU_WHEEL_MODE_ACCELERATED = 1
    };

    static int scaleMenuWheelBurst(int wheel_delta, uint32_t now_ticks, uint32_t& last_tick,
        int& mode, int& burst_direction, int& burst_accumulator,
        uint32_t fast_gap_ticks, uint32_t slow_gap_ticks, uint32_t reset_gap_ticks,
        int burst_extra_threshold)
    {
        if (wheel_delta == 0) {
            return 0;
        }

        if (fast_gap_ticks < 1) {
            fast_gap_ticks = 1;
        }
        if (slow_gap_ticks < fast_gap_ticks) {
            slow_gap_ticks = fast_gap_ticks;
        }
        if (reset_gap_ticks < slow_gap_ticks) {
            reset_gap_ticks = slow_gap_ticks;
        }
        if ((mode != MENU_WHEEL_MODE_PRECISE) && (mode != MENU_WHEEL_MODE_ACCELERATED)) {
            mode = MENU_WHEEL_MODE_PRECISE;
        }
        if (burst_extra_threshold < 1) {
            burst_extra_threshold = 1;
        }

        int direction = (wheel_delta > 0) ? 1 : -1;
        int magnitude = (wheel_delta > 0) ? wheel_delta : -wheel_delta;

        if (burst_direction == 0) {
            burst_direction = direction;
            burst_accumulator = 0;
            mode = MENU_WHEEL_MODE_PRECISE;
            last_tick = now_ticks;
            return direction;
        }

        uint32_t delta_ticks = now_ticks - last_tick;

        if (delta_ticks >= reset_gap_ticks) {
            burst_direction = direction;
            burst_accumulator = 0;
            mode = MENU_WHEEL_MODE_PRECISE;
            last_tick = now_ticks;
            return direction;
        }

        if (mode == MENU_WHEEL_MODE_ACCELERATED) {
            if (direction != burst_direction) {
                burst_direction = direction;
                burst_accumulator = 0;
                mode = MENU_WHEEL_MODE_PRECISE;
                last_tick = now_ticks;
                return direction;
            }
            last_tick = now_ticks;
            burst_accumulator += magnitude;
            int steps = burst_accumulator / burst_extra_threshold;
            burst_accumulator %= burst_extra_threshold;
            return direction * steps;
        }

        if (direction != burst_direction) {
            return 0;
        }

        // Track the last same-direction activity so long cleanup rebound in the
        // opposite direction does not keep the menu locked after the notch has
        // effectively finished.
        last_tick = now_ticks;

        if (delta_ticks < fast_gap_ticks) {
            mode = MENU_WHEEL_MODE_ACCELERATED;
            burst_accumulator += magnitude;
            int steps = burst_accumulator / burst_extra_threshold;
            burst_accumulator %= burst_extra_threshold;
            return direction * steps;
        }

        return 0;
    }

    static void applyWheelAxisDeltas(int16_t& mouse_x, int16_t& mouse_y, int wheel_h_delta, int wheel_v_delta)
    {
        mouse_x += wheel_h_delta;
        mouse_y -= wheel_v_delta;
    }

    static void applyWheelAxis(int16_t& mouse_x, int16_t& mouse_y, int wheel_h, int wheel_v, int scroll_factor)
    {
        int horizontal_remainder = 0;
        int vertical_remainder = 0;
        applyWheelAxisDeltas(mouse_x, mouse_y,
            scaleHorizontalWheelAxisDelta(wheel_h, scroll_factor, horizontal_remainder),
            scaleVerticalWheelAxisDelta(wheel_v, scroll_factor, vertical_remainder));
    }
};

class HidItemList
{
  public:
    bool hasReportID;
    HidReport *inputReportList[256];
    HidReport *outputReportList[256];
    HidReport *featureReportList[256];

    HidItemList()
    {
        hasReportID = false;
        memset(inputReportList, 0, sizeof(inputReportList));
        memset(outputReportList, 0, sizeof(outputReportList));
        memset(featureReportList, 0, sizeof(featureReportList));
    }

    ~HidItemList()
    {
        reset();
    }

    void reset()
    {
        for (int i = 0; i < 256; i++) {
            if (inputReportList[i])
                delete inputReportList[i];
            if (outputReportList[i])
                delete outputReportList[i];
            if (featureReportList[i])
                delete featureReportList[i];
        }
        hasReportID = false;
        memset(inputReportList, 0, sizeof(inputReportList));
        memset(outputReportList, 0, sizeof(outputReportList));
        memset(featureReportList, 0, sizeof(featureReportList));
    }

    bool getInputItem(uint32_t appl, uint32_t id, t_item_location& loc)
    {
        for (int i = 0; i < 256; i++) {
            HidReport *r = inputReportList[i];
            if (!r) {
                continue;
            }
            if (r->applicationUsage != appl) {
                continue;
            }
            int offset = hasReportID ? 8 : 0;
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                if (item->usage == id) {
                    loc.report = r->reportId;
                    loc.offset = offset;
                    loc.length = item->length;
                    loc.count = item->count;
                    loc.min = item->min;
                    loc.max = item->max;
                    loc.flags = item->flags;
                    return true;
                }
                offset += item->length * item->count;
            }
        }
        return false;
    }

    bool locateMouseFields(t_hid_mouse_fields& fields)
    {
        memset(&fields, 0, sizeof(fields));
        fields.valid = getInputItem(0x10002, 0x10030, fields.mouse_x);
        fields.valid &= getInputItem(0x10002, 0x10031, fields.mouse_y);
        fields.valid &= (fields.mouse_x.flags & FLAGS_RELATIVE) != 0;
        fields.valid &= (fields.mouse_y.flags & FLAGS_RELATIVE) != 0;
        fields.has_button1 = getInputItem(0x10002, 0x90001, fields.button1);
        fields.has_button2 = getInputItem(0x10002, 0x90002, fields.button2);
        fields.has_button3 = getInputItem(0x10002, 0x90003, fields.button3);
        fields.has_wheel_v = getInputItem(0x10002, 0x10038, fields.wheel_v);
        fields.has_wheel_h = getInputItem(0x10002, 0xC0238, fields.wheel_h);
        if (fields.has_wheel_v && !(fields.wheel_v.flags & FLAGS_RELATIVE)) {
            fields.has_wheel_v = false;
        }
        if (fields.has_wheel_h && !(fields.wheel_h.flags & FLAGS_RELATIVE)) {
            fields.has_wheel_h = false;
        }
        return fields.valid;
    }

    bool locateKeyboardFields(t_hid_keyboard_fields& fields)
    {
        memset(&fields, 0, sizeof(fields));

        for (int i = 0; i < 256; i++) {
            HidReport *r = inputReportList[i];
            if (!r) {
                continue;
            }
            if (r->applicationUsage != 0x10006) {
                continue;
            }

            int offset = hasReportID ? 8 : 0;
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                t_item_location loc;
                loc.report = r->reportId;
                loc.offset = offset;
                loc.length = item->length;
                loc.count = item->count;
                loc.min = item->min;
                loc.max = item->max;
                loc.flags = item->flags;

                if (item->flags & FLAGS_VARIABLE) {
                    if ((item->usage >= 0x700E0) && (item->usage <= 0x700E7)) {
                        int modifier = item->usage - 0x700E0;
                        fields.has_modifier[modifier] = true;
                        fields.modifiers[modifier] = loc;
                    } else if ((item->usage >= 0x70000) && (item->usage <= 0x700FF)) {
                        fields.has_bitmap_keys = true;
                    }
                } else if (item->usage == 0x70000) {
                    fields.has_key_array = true;
                    fields.key_array = loc;
                }
                offset += item->length * item->count;
            }
        }

        fields.valid = fields.has_key_array || fields.has_bitmap_keys;
        return fields.valid;
    }

    bool synthesizeBootKeyboardReport(const uint8_t *data, int data_len, uint8_t out[8])
    {
        memset(out, 0, 8);
        if (data_len <= 0) {
            return false;
        }
        bool matched_report = false;
        uint8_t current_report = hasReportID ? data[0] : 0;

        for (int i = 0; i < 256; i++) {
            HidReport *r = inputReportList[i];
            if (!r) {
                continue;
            }
            if (r->applicationUsage != 0x10006) {
                continue;
            }
            if (hasReportID && (r->reportId != current_report)) {
                continue;
            }

            matched_report = true;
            int offset = hasReportID ? 8 : 0;
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                t_item_location loc;
                loc.report = r->reportId;
                loc.offset = offset;
                loc.length = item->length;
                loc.count = item->count;
                loc.min = item->min;
                loc.max = item->max;
                loc.flags = item->flags;

                if (item->flags & FLAGS_VARIABLE) {
                    if ((item->usage >= 0x700E0) && (item->usage <= 0x700E7)) {
                        if (HidReport::getValueFromData(data, data_len, loc)) {
                            out[0] |= 1 << (item->usage - 0x700E0);
                        }
                    } else if ((item->usage >= 0x70000) && (item->usage <= 0x700FF)) {
                        if (HidReport::getValueFromData(data, data_len, loc)) {
                            appendKey(out, item->usage & 0xFF);
                        }
                    }
                } else if (item->usage == 0x70000) {
                    for (uint32_t n = 0; n < item->count; n++) {
                        appendKey(out, HidReport::getArrayValueFromData(data, data_len, loc, n));
                    }
                }
                offset += item->length * item->count;
            }
        }
        return matched_report;
    }

    bool hasInputReportId(uint8_t report_id)
    {
        return inputReportList[report_id] != NULL;
    }

  private:
    static void appendKey(uint8_t out[8], int key)
    {
        if ((key <= 0) || (key > 0xFF)) {
            return;
        }
        for (int i = 2; i < 8; i++) {
            if (out[i] == key) {
                return;
            }
        }
        for (int i = 2; i < 8; i++) {
            if (out[i] == 0) {
                out[i] = key;
                return;
            }
        }
    }

    void dump(void)
    {
        char buffer[64];

        printf("The reports %shave a report ID prepended.\n", hasReportID ? "" : "do NOT ");

        for (int i = 0; i < 256; i++) {
            HidReport *r = inputReportList[i];
            if (!r) {
                continue;
            }
            printf("Input Report %d - Application Usage: %08x\n", i, r->applicationUsage);
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                printf("  %08x, %d bits, (%d-%d) %s\n", item->usage, item->length, item->min, item->max,
                       r->getFlagsString(buffer, item));
            }
            printf("\n");
        }

        for (int i = 0; i < 256; i++) {
            HidReport *r = outputReportList[i];
            if (!r) {
                continue;
            }
            printf("Output Report %d - Application Usage: %08x\n", i, r->applicationUsage);
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                printf("  %08x, %d bits, (%d-%d) %s\n", item->usage, item->length, item->min, item->max,
                       r->getFlagsString(buffer, item));
            }
            printf("\n");
        }

        for (int i = 0; i < 256; i++) {
            HidReport *r = featureReportList[i];
            if (!r) {
                continue;
            }
            printf("Feature Report %d - Application Usage: %08x\n", i, r->applicationUsage);
            for (int t = 0; t < r->items.get_elements(); t++) {
                t_reportItem *item = r->items[t];
                printf("  %08x, %d bits, (%d-%d) %s\n", item->usage, item->length, item->min, item->max,
                       r->getFlagsString(buffer, item));
            }
            printf("\n");
        }
    }
};

class HidReportParser
{
    Stack<t_globals *> globalsStack;
    Stack<UsageFifo *> usageFifoStack;
    UsageFifo *usageFifo;
    t_globals globals;
    t_locals locals;
    uint32_t applicationUsage;

    int getValue(const uint8_t *descriptor, int i, uint8_t size, uint32_t *value, int *signed_value)
    {
        *value = 0;
        const int bytes[4] = {0, 1, 2, 4};
        for (int x = 0; x < bytes[size]; x++) {
            *value |= ((uint32_t)descriptor[i + x]) << (8 * x);
        }
        *signed_value = *value;
        switch (size) {
        case 1:
            *signed_value = (*value & 0x80) ? (int)*value - 256 : (int)*value;
            break;
        case 2:
            *signed_value = (*value & 0x8000) ? (int)*value - 0x10000 : (int)*value;
            break;
        }
        return bytes[size];
    }

    typedef enum { INPUT = 0, OUTPUT, FEATURE } t_report_type;

    HidReport *getReport(HidReport **reportList, uint32_t idx)
    {
        if (idx > 255) {
            idx = 255;
        }
        if (!reportList[idx]) {
            reportList[idx] = new HidReport(applicationUsage, idx);
        }
        return reportList[idx];
    }

    void iterate(HidReport **reportList, uint32_t flags)
    {
        if (flags & FLAGS_CONSTANT) {
            getReport(reportList, globals.report_id)
                ->addItem(0, flags, globals.report_count * globals.report_size, 1, 0, 0);
        } else if (flags & FLAGS_VARIABLE) {
            for (uint32_t i = 0; i < globals.report_count; i++) {
                uint32_t usage = usageFifo->pop();
                getReport(reportList, globals.report_id)
                    ->addItem(usage, flags, globals.report_size, 1, globals.logical_minimum, globals.logical_maximum);
            }
        } else {
            getReport(reportList, globals.report_id)
                ->addItem(usageFifo->head(), flags, globals.report_size, globals.report_count,
                          globals.logical_minimum, globals.logical_maximum);
            for (int i = globals.logical_minimum; i <= globals.logical_maximum; i++) {
                usageFifo->pop();
            }
        }
    }

    void clearLocal(void) { memset(&locals, 0, sizeof(t_locals)); }

    void clearGlobal(void) { memset(&globals, 0, sizeof(t_globals)); }

    void cleanup(void)
    {
        do {
            if (usageFifo)
                delete usageFifo;
            if (usageFifoStack.is_empty())
                break;
            usageFifo = usageFifoStack.pop();
        } while (1);
    }

    void init(void)
    {
        clearLocal();
        clearGlobal();
        usageFifo = new UsageFifo();
        applicationUsage = 0;
    }

  public:
    HidReportParser() : globalsStack(8, NULL), usageFifoStack(8, NULL)
    {
        usageFifo = NULL;
        applicationUsage = 0;
    }

    ~HidReportParser() { cleanup(); }

    int decode(HidItemList *result, const uint8_t *descriptor, int length)
    {
        cleanup();
        init();
        result->reset();

        t_globals *tempGlobals;
        uint32_t usage;

        for (int i = 0; i < length;) {
            uint8_t cmd = descriptor[i++];
            uint32_t value = 0;
            int signed_value = 0;

            static const int size_bytes[4] = {0, 1, 2, 4};
            int need = size_bytes[cmd & SIZE_MASK];
            if (i + need > length) {
                return -1; // truncated descriptor
            }
            int advance = getValue(descriptor, i, cmd & SIZE_MASK, &value, &signed_value);
            i += advance;
            switch (cmd & ITEM_MASK) {
            case GLOBAL_PUSH:
                tempGlobals = new t_globals;
                *tempGlobals = globals; // struct copy
                globalsStack.push(tempGlobals);
                break;
            case GLOBAL_POP:
                if (!globalsStack.is_empty()) {
                    tempGlobals = globalsStack.pop();
                    globals = *tempGlobals; // struct copy
                    delete tempGlobals;
                }
                break;
            case GLOBAL_REPORT_ID:
                result->hasReportID = true;
                globals.report_id = value;
                break;
            case GLOBAL_USAGE_PAGE:
                globals.usage_page = value;
                break;
            case GLOBAL_LOGICAL_MIN:
                globals.logical_minimum = signed_value;
                break;
            case GLOBAL_LOGICAL_MAX:
                globals.logical_maximum = signed_value;
                break;
            case GLOBAL_PHYSICAL_MIN:
                globals.physical_minimum = signed_value;
                break;
            case GLOBAL_PHYSICAL_MAX:
                globals.physical_maximum = signed_value;
                break;
            case GLOBAL_UNIT_EXP:
                globals.unit_exp = signed_value;
                break;
            case GLOBAL_UNIT:
                globals.unit = signed_value;
                break;
            case GLOBAL_REPORT_SIZE:
                globals.report_size = value;
                break;
            case GLOBAL_REPORT_COUNT:
                globals.report_count = value;
                break;
            case LOCAL_USAGE_MIN:
                locals.usage_min = value;
                break;
            case LOCAL_DESIGNATOR_IDX:
                locals.designator_idx = value;
                break;
            case LOCAL_DESIGNATOR_MIN:
                locals.designator_min = value;
                break;
            case LOCAL_DESIGNATOR_MAX:
                locals.designator_max = value;
                break;
            case LOCAL_STRING_IDX:
                locals.string_idx = value;
                break;
            case LOCAL_STRING_MIN:
                locals.string_min = value;
                break;
            case LOCAL_STRING_MAX:
                locals.string_max = value;
                break;
            case LOCAL_DELIMITER:
                break;

            case MAIN_COLLECTION:
                usage = usageFifo->pop();
                usageFifoStack.push(usageFifo);
                usageFifo = new UsageFifo();
                switch (value) {
                case COLLECTION_LOGICAL:
                    break;
                case COLLECTION_PHYSICAL:
                    break;
                case COLLECTION_APPLICATION:
                    applicationUsage = usage;
                    break;
                }
                break;
            case MAIN_END_COLL:
                delete usageFifo;
                usageFifo = usageFifoStack.pop();
                break;
            case MAIN_INPUT:
                iterate(result->inputReportList, value);
                break;
            case MAIN_OUTPUT:
                iterate(result->outputReportList, value);
                break;
            case MAIN_FEATURE:
                iterate(result->featureReportList, value);
                break;
            case LOCAL_USAGE:
                usageFifo->push(globals.usage_page << 16 | value);
                break;
            case LOCAL_USAGE_MAX:
                for (uint32_t n = locals.usage_min & 0xFFFF; n <= (value & 0xFFFF); n++) {
                    usageFifo->push(globals.usage_page << 16 | n);
                }
                break;
            default:
                return -1;
            }

            if ((cmd & TYPE_MASK) == TYPE_MAIN) {
                clearLocal();
            }
        }
        return 0;
    }
};
#endif // _HID_DECODER_H
